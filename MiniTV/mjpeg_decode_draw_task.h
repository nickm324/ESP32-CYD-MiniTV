#define READ_BUFFER_SIZE 1024
// JPEGDEC emits 16-pixel-wide MCU columns. The callback size must divide the
// image's MCU width or this decoder version can omit the remainder columns.
// 288 / 16 = 18 MCUs (groups of 6); 320 / 16 = 20 MCUs (groups of 5).
#ifdef BOARD_MINI_TV
#define MAXOUTPUTSIZE 6
#else
#define MAXOUTPUTSIZE 5
#endif
#define NUMBER_OF_DECODE_BUFFER 1
#define NUMBER_OF_DRAW_BUFFER 1

#include "config.h"
#include <FS.h>
#include <JPEGDEC.h>

typedef struct
{
  int32_t size;
  uint8_t *buf;
} mjpegBuf;

typedef struct
{
  xQueueHandle xqh;
  JPEG_DRAW_CALLBACK *drawFunc;
} paramDrawTask;

typedef struct
{
  xQueueHandle xqh;
  mjpegBuf *mBuf;
  JPEG_DRAW_CALLBACK *drawFunc;
} paramDecodeTask;

static JPEGDRAW jpegdraws[NUMBER_OF_DRAW_BUFFER];
static int _draw_queue_cnt = 0;
static JPEGDEC _jpegDec;
static xQueueHandle _xqh;
static bool _useBigEndian;

static unsigned long total_read_video_ms = 0;
static unsigned long total_decode_video_ms = 0;
static unsigned long total_show_video_ms = 0;

Stream *_input;

int32_t _mjpegBufSize;

uint8_t *_read_buf;
int32_t _mjpeg_buf_offset = 0;

TaskHandle_t _decodeTask;
TaskHandle_t _draw_task;
paramDecodeTask _pDecodeTask;
paramDrawTask _pDrawTask;
uint8_t *_mjpeg_buf;
uint8_t _mBufIdx = 0;

int32_t _inputindex = 0;
int32_t _buf_read;
int32_t _remain = 0;
mjpegBuf _mjpegBufs[NUMBER_OF_DECODE_BUFFER];

static int queueDrawMCU(JPEGDRAW *pDraw)
{
  int len = pDraw->iWidth * pDraw->iHeight * 2;
  JPEGDRAW *j = &jpegdraws[_draw_queue_cnt % NUMBER_OF_DRAW_BUFFER];
  j->x = pDraw->x;
  j->y = pDraw->y;
  j->iWidth = pDraw->iWidth;
  j->iHeight = pDraw->iHeight;
  memcpy(j->pPixels, pDraw->pPixels, len);

  // log_i("queueDrawMCU start.");
  ++_draw_queue_cnt;
  xQueueSend(_xqh, &j, portMAX_DELAY);
  // log_i("queueDrawMCU end.");

  return 1;
}

static void decode_task(void *arg)
{
  paramDecodeTask *p = (paramDecodeTask *)arg;
  mjpegBuf *mBuf;
  log_i("decode_task start.");
  while (xQueueReceive(p->xqh, &mBuf, portMAX_DELAY))
  {
    // log_i("mBuf->size: %d", mBuf->size);
    // log_i("mBuf->buf start: %X %X, end: %X, %X.", mBuf->buf[0], mBuf->buf[1], mBuf->buf[mBuf->size - 2], mBuf->buf[mBuf->size - 1]);
    unsigned long s = millis();

    _jpegDec.openRAM(mBuf->buf, mBuf->size, p->drawFunc);

    // _jpegDec.setMaxOutputSize(MAXOUTPUTSIZE);
    if (_useBigEndian)
    {
      _jpegDec.setPixelType(RGB565_BIG_ENDIAN);
    }
    _jpegDec.setMaxOutputSize(MAXOUTPUTSIZE);
    _jpegDec.decode(0, 0, 0);
    _jpegDec.close();

    total_decode_video_ms += millis() - s;
  }
  vQueueDelete(p->xqh);
  log_i("decode_task end.");
  vTaskDelete(NULL);
}

static void draw_task(void *arg)
{
  paramDrawTask *p = (paramDrawTask *)arg;
  JPEGDRAW *pDraw;
  log_i("draw_task start.");
  while (xQueueReceive(p->xqh, &pDraw, portMAX_DELAY))
  {
    // log_i("draw_task work start: x: %d, y: %d, iWidth: %d, iHeight: %d.", pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight);
    p->drawFunc(pDraw);
    // log_i("draw_task work end.");
  }
  vQueueDelete(p->xqh);
  log_i("draw_task end.");
  vTaskDelete(NULL);
}

bool mjpeg_setup(Stream *input, int32_t mjpegBufSize, JPEG_DRAW_CALLBACK *pfnDraw,
                  bool useBigEndian, BaseType_t decodeAssignCore, BaseType_t drawAssignCore)
{
  _input = input;
  _mjpegBufSize = mjpegBufSize;
  _useBigEndian = useBigEndian;
  _inputindex = 0;
  _buf_read = 0;
  _remain = 0;
  _mjpeg_buf_offset = 0;
  _mBufIdx = 0;

  for (int i = 0; i < NUMBER_OF_DECODE_BUFFER; ++i)
  {
    // Reuse the frame buffer between videos. The original reboot-based player
    // allocated a new buffer for every video and relied on rebooting to free it.
    if (!_mjpegBufs[i].buf)
    {
      _mjpegBufs[i].buf = (uint8_t *)malloc(mjpegBufSize);
    }
    if (_mjpegBufs[i].buf)
    {
      log_i("#%d decode buffer allocated.", i);
    }
    else
    {
      log_e("#%d decode buffer allocat failed.", i);
    }
  }
  _mjpeg_buf = _mjpegBufs[_mBufIdx].buf;

  if (!_read_buf)
  {
    _read_buf = (uint8_t *)malloc(READ_BUFFER_SIZE);
  }
  if (_read_buf)
  {
    log_i("Read buffer allocated.");
  }

  // Diagnostic path: decode and draw synchronously. Do not create background
  // queues/tasks, which otherwise keep another JPEGDEC user alive.
  _pDecodeTask.drawFunc = pfnDraw;

  return true;
}

bool mjpeg_read_frame()
{
  if (_inputindex == 0)
  {
    _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
    _inputindex += _buf_read;
  }
  _mjpeg_buf_offset = 0;
  int i = 0;
  bool found_FFD8 = false;
  while ((_buf_read > 0) && (!found_FFD8))
  {
    i = 0;
    while ((i < _buf_read) && (!found_FFD8))
    {
      if ((_read_buf[i] == 0xFF) && (_read_buf[i + 1] == 0xD8)) // JPEG header
      {
        // log_i("Found FFD8 at: %d.", i);
        found_FFD8 = true;
      }
      ++i;
    }
    if (found_FFD8)
    {
      --i;
    }
    else
    {
      _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
    }
  }
  uint8_t *_p = _read_buf + i;
  _buf_read -= i;
  bool found_FFD9 = false;
  if (_buf_read > 0)
  {
    i = 3;
    while ((_buf_read > 0) && (!found_FFD9))
    {
      if ((_mjpeg_buf_offset > 0) && (_mjpeg_buf[_mjpeg_buf_offset - 1] == 0xFF) && (_p[0] == 0xD9)) // JPEG trailer
      {
        found_FFD9 = true;
      }
      else
      {
        while ((i < _buf_read) && (!found_FFD9))
        {
          if ((_p[i] == 0xFF) && (_p[i + 1] == 0xD9)) // JPEG trailer
          {
            found_FFD9 = true;
            ++i;
          }
          ++i;
        }
      }

      // log_i("i: %d", i);
      if ((i < 0) || (_mjpeg_buf_offset > _mjpegBufSize - i))
      {
        log_e("MJPEG frame exceeds buffer: need at least %d bytes, have %d",
              _mjpeg_buf_offset + i, _mjpegBufSize);
        return false;
      }
      memcpy(_mjpeg_buf + _mjpeg_buf_offset, _p, i);
      _mjpeg_buf_offset += i;
      int32_t o = _buf_read - i;
      if (o > 0)
      {
        // log_i("o: %d", o);
        memcpy(_read_buf, _p + i, o);
        _buf_read = _input->readBytes(_read_buf + o, READ_BUFFER_SIZE - o);
        _p = _read_buf;
        _inputindex += _buf_read;
        _buf_read += o;
        // log_i("_buf_read: %d", _buf_read);
      }
      else
      {
        _buf_read = _input->readBytes(_read_buf, READ_BUFFER_SIZE);
        _p = _read_buf;
        _inputindex += _buf_read;
      }
      i = 0;
    }
    if (found_FFD9)
    {
      // log_i("Found FFD9 at: %d.", _mjpeg_buf_offset);
      if (_mjpeg_buf_offset > _mjpegBufSize) {
        log_e("_mjpeg_buf_offset(%d) > _mjpegBufSize (%d)", _mjpeg_buf_offset, _mjpegBufSize);
      }
      return true;
    }
  }

  return false;
}

bool mjpeg_draw_frame()
{
  if (!_jpegDec.openRAM(
          _mjpeg_buf,
          _mjpeg_buf_offset,
          _pDecodeTask.drawFunc))
  {
    Serial.println("JPEG open failed");
    return false;
  }

  static int reportedWidth = -1;
  static int reportedHeight = -1;
  const int frameWidth = _jpegDec.getWidth();
  const int frameHeight = _jpegDec.getHeight();
  if ((frameWidth != reportedWidth) || (frameHeight != reportedHeight))
  {
    Serial.printf("Decoded MJPEG frame size: %d x %d\n", frameWidth, frameHeight);
    reportedWidth = frameWidth;
    reportedHeight = frameHeight;
  }

  if (_useBigEndian)
  {
    _jpegDec.setPixelType(RGB565_BIG_ENDIAN);
  }

  _jpegDec.setMaxOutputSize(MAXOUTPUTSIZE);
  int result = _jpegDec.decode(0, 0, 0);
  _jpegDec.close();

  return result == 1;
}
