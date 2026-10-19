package cc.polysfaer.vkframework.samples.audio_visualizer

import androidx.media3.common.C
import androidx.media3.common.audio.AudioProcessor
import androidx.media3.common.audio.AudioProcessor.AudioFormat
import androidx.media3.common.audio.BaseAudioProcessor
import androidx.media3.common.util.UnstableApi
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * Pass-through audio processor that taps the decoded PCM, downmixes it to
 * mono and publishes a magnitude spectrum every HOP samples.
 *
 * Magnitudes are scaled so that a full-scale sine reads about 1.0 (0 dB).
 * Bin k covers k * sampleRate / FFT_SIZE Hz; there are FFT_SIZE / 2 bins.
 */
@UnstableApi
class SpectrumAudioProcessor(
  private val onSpectrum: (magnitudes: FloatArray, sampleRateHz: Int) -> Unit
) : BaseAudioProcessor() {

  private companion object {
    const val FFT_SIZE = 1024
    const val HOP = 512
  }

  private val window = FloatArray(FFT_SIZE) {
    (0.5 - 0.5 * cos(2.0 * PI * it / FFT_SIZE)).toFloat()
  }
  private val windowSum = window.sum()
  private val cosTable = FloatArray(FFT_SIZE / 2) {
    cos(2.0 * PI * it / FFT_SIZE).toFloat()
  }
  private val sinTable = FloatArray(FFT_SIZE / 2) {
    sin(2.0 * PI * it / FFT_SIZE).toFloat()
  }
  private val bitReverse = IntArray(FFT_SIZE).also { table ->
    val bits = Integer.numberOfTrailingZeros(FFT_SIZE)
    for (i in 0 until FFT_SIZE) {
      table[i] = Integer.reverse(i) ushr (32 - bits)
    }
  }

  private val ring = FloatArray(FFT_SIZE)
  private val re = FloatArray(FFT_SIZE)
  private val im = FloatArray(FFT_SIZE)
  private val magnitudes = FloatArray(FFT_SIZE / 2)

  private var writePos = 0
  private var sinceHop = 0
  private var channelCount = 2
  private var sampleRate = 48000
  private var isFloat = false

  // ---------------------------------------------------

  override fun onConfigure(inputAudioFormat: AudioFormat): AudioFormat {
    if (
      inputAudioFormat.encoding != C.ENCODING_PCM_16BIT &&
      inputAudioFormat.encoding != C.ENCODING_PCM_FLOAT
    ) {
      throw AudioProcessor.UnhandledAudioFormatException(inputAudioFormat)
    }

    channelCount = inputAudioFormat.channelCount
    sampleRate = inputAudioFormat.sampleRate
    isFloat = inputAudioFormat.encoding == C.ENCODING_PCM_FLOAT

    return inputAudioFormat
  }

  override fun onFlush() {
    ring.fill(0f)
    writePos = 0
    sinceHop = 0
  }

  override fun queueInput(inputBuffer: ByteBuffer) {
    val size = inputBuffer.remaining()
    if (size == 0) {
      return
    }

    analyze(inputBuffer.duplicate().order(ByteOrder.nativeOrder()))
    replaceOutputBuffer(size).put(inputBuffer).flip()
  }

  // ---------------------------------------------------

  private fun analyze(buf: ByteBuffer) {
    val frameSize = channelCount * (if (isFloat) 4 else 2)

    while (buf.remaining() >= frameSize) {
      var sum = 0f

      for (c in 0 until channelCount) {
        sum += if (isFloat) buf.getFloat() else buf.getShort() / 32768f
      }

      ring[writePos] = sum / channelCount
      writePos = (writePos + 1) and (FFT_SIZE - 1)

      if (++sinceHop >= HOP) {
        sinceHop = 0
        computeSpectrum()
      }
    }
  }

  private fun computeSpectrum() {
    var idx = writePos

    for (i in 0 until FFT_SIZE) {
      re[i] = ring[idx] * window[i]
      im[i] = 0f
      idx = (idx + 1) and (FFT_SIZE - 1)
    }

    fft()

    for (k in 0 until FFT_SIZE / 2) {
      magnitudes[k] = 2f * sqrt(re[k] * re[k] + im[k] * im[k]) / windowSum
    }

    onSpectrum(magnitudes, sampleRate)
  }

  // In-place iterative radix-2 FFT.
  private fun fft() {
    for (i in 0 until FFT_SIZE) {
      val j = bitReverse[i]

      if (j > i) {
        val tr = re[i]; re[i] = re[j]; re[j] = tr
        val ti = im[i]; im[i] = im[j]; im[j] = ti
      }
    }

    var len = 2

    while (len <= FFT_SIZE) {
      val half = len / 2
      val step = FFT_SIZE / len
      var start = 0

      while (start < FFT_SIZE) {
        for (k in 0 until half) {
          val wr = cosTable[k * step]
          val wi = -sinTable[k * step]
          val a = start + k
          val b = a + half

          val tr = re[b] * wr - im[b] * wi
          val ti = re[b] * wi + im[b] * wr

          re[b] = re[a] - tr
          im[b] = im[a] - ti
          re[a] += tr
          im[a] += ti
        }

        start += len
      }

      len = len shl 1
    }
  }
}
