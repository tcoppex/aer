package cc.polysfaer.vkframework.samples.audio_visualizer

import android.Manifest
import android.app.NativeActivity
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat

class MainActivity : NativeActivity() {
  private var isInitialized = false;

  companion object {
    init {
      System.loadLibrary("audio_visualizer")
    }
  }

  // ----------------------------------------------

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
  }

  override fun onResume() {
    super.onResume();

    if (!isInitialized) {
      if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
        ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.RECORD_AUDIO), 0)
      } else {
        startApp();
      }
    }
  }

  override fun onDestroy() {
    super.onDestroy()
  }

  override fun onRequestPermissionsResult(
    requestCode: Int,
    permissions: Array<out String>,
    grantResults: IntArray
  ) {
    super.onRequestPermissionsResult(requestCode, permissions, grantResults)
    if (requestCode == 0) {
      startApp()
    }
  }

  // ----------------------------------------------

  private fun startApp() {
    if (isInitialized) {
      return;
    }
    isInitialized = true;
  }
}