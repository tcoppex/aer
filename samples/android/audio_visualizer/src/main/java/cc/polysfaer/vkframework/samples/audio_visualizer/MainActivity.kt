
package cc.polysfaer.vkframework.samples.audio_visualizer

import android.Manifest
import android.app.NativeActivity
import android.content.Context
import android.content.pm.PackageManager
import android.os.Bundle
import android.util.Log
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.exoplayer.DefaultRenderersFactory
import androidx.media3.exoplayer.audio.AudioSink
import androidx.media3.exoplayer.audio.DefaultAudioSink
import androidx.media3.exoplayer.ExoPlayer
import java.util.concurrent.atomic.AtomicBoolean

class MainActivity : NativeActivity() {

  companion object {
    private const val TAG = "AudioVisualizer"
    private const val REQUEST_RECORD_AUDIO = 0

    private const val STREAM_URL = "https://differentdrumz.radioca.st/electronic.mp3"

    init {
      System.loadLibrary("audio_visualizer")
    }
  }

  private var player: ExoPlayer? = null
  private var isInitialized = false
  private val isDestroyed = AtomicBoolean(false)

  // ---------------------------------------------------

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
  }

  override fun onResume() {
    super.onResume()

    if (isInitialized) {
      return
    }

    val granted = ContextCompat.checkSelfPermission(
      this, Manifest.permission.RECORD_AUDIO
    ) == PackageManager.PERMISSION_GRANTED

    if (granted) {
      startApp()
    } else {
      ActivityCompat.requestPermissions(
        this,
        arrayOf(Manifest.permission.RECORD_AUDIO),
        REQUEST_RECORD_AUDIO
      )
    }
  }

  override fun onDestroy() {
    isDestroyed.set(true)

    player?.release()
    player = null

    super.onDestroy()
  }

  override fun onRequestPermissionsResult(
    requestCode: Int,
    permissions: Array<out String>,
    grantResults: IntArray
  ) {
    super.onRequestPermissionsResult(
      requestCode,
      permissions,
      grantResults
    )

    if (requestCode == REQUEST_RECORD_AUDIO) {
      val granted = grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED

      // Playback does not require RECORD_AUDIO.
      startApp()
    }
  }

  // ---------------------------------------------------

  private fun startApp() {
    if (isInitialized) {
      return
    }
    isInitialized = true

    initExoPlayer(STREAM_URL)
  }

  @OptIn(UnstableApi::class)
  private fun createRenderersFactory(): DefaultRenderersFactory {
    val processor = SpectrumAudioProcessor { magnitudes, sampleRateHz ->
      if (!isDestroyed.get()) {
        nativeUpdateAudioSpectrum(magnitudes, sampleRateHz)
      }
    }

    return object : DefaultRenderersFactory(this) {
      override fun buildAudioSink(
        context: Context,
        enableFloatOutput: Boolean,
        enableAudioTrackPlaybackParams: Boolean
      ): AudioSink {
        return DefaultAudioSink.Builder(context)
          .setEnableFloatOutput(enableFloatOutput)
          .setEnableAudioTrackPlaybackParams(enableAudioTrackPlaybackParams)
          .setAudioProcessors(arrayOf(processor))
          .build()
      }
    }
  }

  private fun initExoPlayer(streamUrl: String) {
    try {
      player?.release()

      player = ExoPlayer.Builder(this, createRenderersFactory()).build().apply {
        addListener(object : Player.Listener {
          override fun onPlaybackStateChanged(state: Int) {
            val stateName = when (state) {
              Player.STATE_IDLE -> "IDLE"
              Player.STATE_BUFFERING -> "BUFFERING"
              Player.STATE_READY -> "READY"
              Player.STATE_ENDED -> "ENDED"
              else -> "UNKNOWN($state)"
            }
            Log.i(TAG, "Playback state: $stateName")
          }

          override fun onAudioSessionIdChanged(audioSessionId: Int) {
            Log.i(TAG, "Audio session changed: $audioSessionId")
          }

          override fun onIsPlayingChanged(isPlaying: Boolean) {
            Log.i(TAG,
              "isPlaying=$isPlaying, " +
                "state=${player?.playbackState}, " +
                "playWhenReady=${player?.playWhenReady}, " +
                "suppression=${player?.playbackSuppressionReason}, " +
                "sessionId=${player?.audioSessionId}"
            )
          }

          override fun onPlayerError(error: PlaybackException) {
            Log.e(TAG, "Playback failed: ${error.errorCodeName}; message=${error.message}", error)
          }
        })

        setMediaItem(MediaItem.fromUri(streamUrl))
        prepare()
        playWhenReady = true
      }
    } catch (e: Exception) {
      Log.e(TAG, "Failed to initialize ExoPlayer", e)
    }
  }


  // ---------------------------------------------------

  external fun nativeUpdateAudioSpectrum(
    magnitudes: FloatArray,
    sampleRateHz: Int
  )
}
