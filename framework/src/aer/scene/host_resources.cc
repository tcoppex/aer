#include "aer/scene/host_resources.h"

#include <iostream>
#include "aer/scene/private/gltf_loader.h"

/* -------------------------------------------------------------------------- */

namespace scene {

void HostResources::setup() {
  // -----------------
  scene.setup();
  // -----------------

  // Create default 1x1 textures for optionnal bindings.
  //  -> should it be left to each MaterialFx?
  {
    constexpr uint32_t kDefaultResourceBufferSize = 128u;
    host_images.reserve(kDefaultResourceBufferSize);
    textures.reserve(kDefaultResourceBufferSize);

    auto push_default_texture{[
      &_textures = this->textures,
      &_host_images = this->host_images
    ] (std::array<uint8_t, 4> const& c) -> uint32_t {
        uint32_t const texture_id = static_cast<uint32_t>(_textures.size());
        _textures.emplace_back( static_cast<uint32_t>(_host_images.size()) );
        _host_images.emplace_back( c[0], c[1], c[2], c[3] );
        return texture_id;
      }
    };

    auto &bindings = default_texture_binding_;
    bindings.basecolor          = push_default_texture({255, 255, 255, 255});
    bindings.normal             = push_default_texture({128, 128, 255, 255});
    bindings.roughness_metallic = push_default_texture({  0, 255,   0,   0});
    bindings.occlusion          = push_default_texture({255, 255, 255, 255});
    bindings.emissive           = push_default_texture({  0,   0,   0,   0});
  }

}

// ----------------------------------------------------------------------------

bool HostResources::loadFile(std::string_view filename) {
  auto const basename{ utils::ExtractBasename(filename) };
  auto const ext{ utils::ExtractExtension(filename) };

  if (!loadGLTF(filename)) {
    return false;
  }

  resetInternalDescriptors();

#ifndef NDEBUG
  LOGI("> \"{}.{}\" has been loaded successfully.", basename, ext);

  // This will also display the extra data procedurally created.
  std::cout << "┌────────────┬───── " << std::endl;
  std::cout << "│ Images     │ " << host_images.size() << std::endl;
  std::cout << "│ Textures   │ " << textures.size() << std::endl;
  std::cout << "│ Materials  │ " << material_proxies.size() << std::endl;
  std::cout << "│ Skeletons  │ " << skeletons.size() << std::endl;
  std::cout << "│ Animations │ " << animations_map.size() << std::endl;
  std::cout << "│ Meshes     │ " << meshes.size() << std::endl;
  std::cerr << "└────────────┴─────" << std::endl;

  // uint32_t const kMegabyte{ 1024u * 1024u };
  // LOGI("> vertex buffer size {} Mb", vertex_buffer_size / static_cast<float>(kMegabyte));
  // LOGI("> index buffer size {} Mb ", index_buffer_size / static_cast<float>(kMegabyte));
  // LOGI("> total image size {} Mb ", total_image_size / static_cast<float>(kMegabyte));
#endif

  return true;
}

// ----------------------------------------------------------------------------

/* [Work In Progress] */
// Mesh* HostResources::createMesh(std::string_view mesh_name) {
//   auto mesh = std::make_unique<Mesh>();

//   // material_refs.push_back( std::make_unique<scene::MaterialRef>(scene::MaterialRef{
//   //   .model = material_model,
//   //   .states = GetMaterialStates(mat),
//   //   .proxy_index = material_index,
//   // }) );

//   meshes.push_back( std::move(mesh) );

//   mesh_indices_map[std::string(mesh_name)] = meshes.size() - 1;
//   transforms.emplace_back( linalg::identity );
//   transforms_has_changed = true;
//   return meshes.back().get();
// }

// ----------------------------------------------------------------------------

// Mesh* HostResources::findMeshByName(std::string_view mesh_name) const {
//   if (auto it = mesh_indices_map.find(std::string(mesh_name)); it != mesh_indices_map.end()) {
//     uint32_t index = it->second;
//     return meshes[index].get();
//   }
//   return nullptr;
// }

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

bool HostResources::loadGLTF(std::string_view filename) {
  auto const basename{ utils::ExtractBasename(filename) };
  auto const ext{ utils::ExtractExtension(filename) };

  cgltf_options options{};
  cgltf_result result{};
  cgltf_data* data{};

  utils::FileReader file{};
  if (!file.read(filename)) {
    LOGE("GLTF: failed to read the file.");
    return false;
  }

  result = cgltf_parse(&options, file.buffer.data(), file.buffer.size(), &data);
  if (cgltf_result_success != result) {
    LOGE("GLTF: failed to parse file \"{}\" {}.\n", basename, (int)result);
    return false;
  }

  result = cgltf_load_buffers(&options, data, filename.data());
  if (cgltf_result_success != result) {
    LOGE("GLTF: failed to load buffers in \"{}\" {}.\n", basename, (int)result);
    cgltf_free(data);
    return false;
  }

  /* Extract data */
  {
    using namespace internal::gltf_loader;

    // ++ Important Note ++
    // Scenes are not parsed, all objects are loaded as part of the
    // same scene.

    // Reserve data.
    samplers.reserve(data->samplers_count + samplers.size());
    host_images.reserve(data->images_count + host_images.size());
    textures.reserve(data->textures_count + textures.size());
    material_proxies.reserve(data->materials_count + material_proxies.size());
    material_refs.reserve(data->materials_count + material_refs.size());
    skeletons.reserve(data->skins_count + skeletons.size());
    meshes.reserve(data->meshes_count + meshes.size());
    transforms.reserve(data->meshes_count + transforms.size());
    animations_map.reserve(data->animations_count);

    if constexpr (kUseAsyncLoad)
    {
      /* --- Async tasks version --- */

      auto run_task         = utils::RunTaskGeneric<void>;
      auto run_task_ret     = utils::RunTaskGeneric<PointerToIndexMap_t>;
      auto run_task_scene   = utils::RunTaskGeneric<PointerToEntityMap_t>;
      auto run_task_sampler = utils::RunTaskGeneric<PointerToSamplerMap_t>;

      auto taskSceneEntities = run_task_scene([
        data,
        &_scene = this->scene
      ] {
        return ExtractSceneHierarchy(data, _scene);
      });

      auto taskSamplers = run_task_sampler([
        data,
        &_samplers = this->samplers
      ] {
        return ExtractSamplers(data, _samplers);
      });

      auto taskSkeletons = run_task_ret([
        data,
        &_skeletons = this->skeletons
      ] {
        return ExtractSkeletons(data, _skeletons);
      });

      // [real bottleneck]
      // Internally images are loaded asynchronously and must be waited for at the end.
      auto taskImageData = run_task_ret([
        data,
        &_host_images = this->host_images
      ] {
        return ExtractImages(data, _host_images);
      });

      auto taskTextures = run_task_ret([
        &taskImageData,
        &taskSamplers,
        data,
        &_textures = this->textures
      ] {
        auto images_indices = taskImageData.get();
        auto samplers_lut = taskSamplers.get();
        return ExtractTextures(data, images_indices, samplers_lut, _textures);
      });

      auto taskMaterials = run_task_ret([
        &taskTextures,
        data,
        &_material_proxies = this->material_proxies,
        &_material_refs = this->material_refs,
        &_default_binding = this->default_texture_binding_
      ] {
        auto textures_indices = taskTextures.get();
        return ExtractMaterials(
          data,
          textures_indices,
          _material_proxies,
          _material_refs,
          _default_binding
        );
      });

      auto skeletons_indices = taskSkeletons.get();

      auto taskAnimations = run_task([
        data,
        &skeletons_indices,
        &_skeletons = this->skeletons
      ] {
        // ExtractAnimations(data, basename, skeletons_indices, _skeletons, animations_map);
      });

      auto taskMeshes = run_task([
        &taskSceneEntities,
        &taskMaterials,
        data,
        &skeletons_indices,
        &_scene = this->scene, //
        &_material_refs = this->material_refs,
        &_skeletons = this->skeletons,
        &_meshes = this->meshes,
        &_mesh_indices_map = this->mesh_indices_map
      ] {
        auto entities_lut = taskSceneEntities.get();
        auto materials_indices = taskMaterials.get();
        ExtractMeshes(
          data,
          _scene,
          entities_lut,
          materials_indices,
          _material_refs,
          skeletons_indices,
          _skeletons,
          _meshes,
          _mesh_indices_map,
          kRestructureAttribs,
          kForce32BitsIndexing
        );
      });

      taskAnimations.get();
      taskMeshes.get();
    }
    else
    {
      /* --- Serialized version --- */

      auto entities_lut       = ExtractSceneHierarchy(data, scene);
      auto samplers_lut       = ExtractSamplers(data, samplers);
      auto skeletons_indices  = ExtractSkeletons(data, skeletons);
      auto images_indices     = ExtractImages(data, host_images);
      auto textures_indices   = ExtractTextures(
        data, images_indices, samplers_lut, textures
      );
      auto materials_indices  = ExtractMaterials(
        data,
        textures_indices,
        material_proxies,
        material_refs,
        default_texture_binding_
      );
      ExtractMeshes(
        data,
        scene,
        entities_lut,
        materials_indices,
        material_refs,
        skeletons_indices,
        skeletons,
        meshes,
        mesh_indices_map,
        kRestructureAttribs,
        kForce32BitsIndexing
      );
    }

    /* Recalculate the scene global matrices buffer. */
    updateTransformsBuffer();

    /* Wait for the host images to finish loading before using them. */
    for (auto & host_image : host_images) {
      host_image.getAsyncResult();
    }
  }

  /* [!] Be sure to have loaded all images before freeing gltf data. */
  cgltf_free(data);

  return true;
}

// ----------------------------------------------------------------------------

void HostResources::resetInternalDescriptors() {
  /* Calculate the offsets to indivual mesh data inside the shared vertices
   * and indices buffers. */
  uint32_t transform_index = 0u;
  vertex_buffer_size = 0u;
  index_buffer_size = 0u;

  for (auto const& mesh : meshes) {
    // ---------
    mesh->transform_index = transform_index++; //
    // ---------

    mesh->set_buffer_info({
      .vertex_offset = vertex_buffer_size,
      .index_offset = index_buffer_size,
    });
    vertex_buffer_size += mesh->vertices_bytesize();
    index_buffer_size += mesh->indices_bytesize();
  }

  for (auto const& host_image : host_images) {
    total_image_size += host_image.bytesize();
  }
}

// ----------------------------------------------------------------------------

void HostResources::updateTransformsBuffer() {
  /* Resize the transform buffer according to mesh count. */
  transforms.resize(meshes.size(), linalg::identity); //

  /* Update the entities hierarchy. */
  scene.update();

  // [wip] Copy new matrices to the local matrices buffer.
  scene.registry
    .view<scene::component::GlobalTransform, scene::component::Mesh>()
    .each([&_transforms = this->transforms](auto &global, auto &mesh) {
      _transforms[mesh.meshIndex] = global.worldMatrix;
    });
}

}  // namespace scene

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

#if !defined(NDEBUG)

/* Checks to assert the data structures can be moved / assigned cheaply. */

STATIC_ASSERT_TRIVIALITY(scene::Sampler);
STATIC_ASSERT_TRIVIALITY(scene::Texture);
STATIC_ASSERT_TRIVIALITY(scene::MaterialProxy);
STATIC_ASSERT_TRIVIALITY(scene::MaterialRef);

STATIC_ASSERT_MOVABLE_ONLY(scene::ImageData);

// [TODO]
// STATIC_ASSERT_MOVABLE_ONLY(scene::Mesh);
// STATIC_ASSERT_MOVABLE_ONLY(scene::Skeleton);

#endif // NDEBUG

/* -------------------------------------------------------------------------- */
