#include <Arduino.h>

#include <complex>
#include <math.h>
//#include <ESP32-RGB64x32MatrixPanel-I2S-DMA.h>
//RGB64x32MatrixPanel_I2S_DMA dma_display;
#define MATRIX_WIDTH 64
#define MATRIX_HEIGHT 64
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
MatrixPanel_I2S_DMA dma_display;

#define R1_PIN  2
#define G1_PIN  15
#define B1_PIN  4
#define R2_PIN  32
#define G2_PIN  27
#define B2_PIN  23

#define A_PIN   5
#define B_PIN   18
#define C_PIN   19
#define D_PIN   21
#define E_PIN   33 // Change to a valid pin if using a 64 pixel row panel.

#define LAT_PIN 26
#define OE_PIN  25

#define CLK_PIN 22


// Or use an Alternative non-DMA library, i.e:
//#include <P3RGB64x32MatrixPanel.h>
//P3RGB64x32MatrixPanel display;
const int max_iter = 256;
float SCALEX = 1, SCALEY = 1, MOVEX = 0.0, MOVEY = 0.0;

class rgb_24
{
public:
  uint8_t r;
  uint8_t g;
  uint8_t b;

  rgb_24() { r= g= b= 0;}
  rgb_24(uint8_t r, uint8_t g, uint8_t b) : r(r), g(g), b(b) {}
  ~rgb_24() = default;

};

static rgb_24 ct[256];

struct tparam_t
{
  int tno;
  int xl, xh, yl, yh;
  char s[256];
  SemaphoreHandle_t sem, go;

  tparam_t(int t, int x1, int x2, int y1, int y2, SemaphoreHandle_t se)
  {
    tno = t;
    xl = x1;
    xh = x2;
    yl = y1;
    yh = y2;
    sem = se;
    sprintf(s, "t=%d, xl=%d,xh=%d,yl=%d,yh=%d", tno, xl, xh, yl, yh);
    go = xSemaphoreCreateBinary();
    xSemaphoreGive(go);
    xSemaphoreTake(go, portMAX_DELAY); // now initialized to block
  }
  char *toString() { return s; }
};

inline float abs2(std::complex<float> f)
{
  float r = f.real(), i = f.imag();
  return r * r + i * i;
}
static int mandel_calc_point(int x, int y, int width, int height)
{
  const std::complex<float> point((float)x / (width * SCALEX) - (1.5 + MOVEX), (float)y / (height * SCALEY) - (0.5 + MOVEY));
  // we divide by the image dimensions to get values smaller than 1
  // then apply a translation
  std::complex<float> z = point;
  unsigned int nb_iter = 1;
  while (abs2(z) < 4 && nb_iter <= max_iter)
  {
    z = z * z + point;
    nb_iter++;
  }
  if (nb_iter < max_iter)
    return (nb_iter);
  else
    return 0;
}

void mandel_helper(int xl, int xh, int yl, int yh)
{
  int x, y, d;

  for (x = xl; x <= xh; x++)
  {
    for (y = yl; y <= yh; y++)
    {
      d = mandel_calc_point(x, y, 64, 64);
      //dma_display.drawPixelRGB24(x, y, ct[d]);
      dma_display.drawPixelRGB888(x, y, ct[d].r, ct[d].g, ct[d].b);
      taskYIELD();
    }
  }
}

void mandel_wrapper(void *param)
{
  tparam_t *p = (tparam_t *)param;
  //Serial.print(pcTaskGetTaskName(NULL)); Serial.print(p->tno); Serial.println(" started.");
  //Serial.println(p->toString());
  for (;;)
  {
    mandel_helper(p->xl, p->xh, p->yl, p->yh);
    xSemaphoreGive(p->sem);
    xSemaphoreTake(p->go, portMAX_DELAY);
    delay(10);
  }
}

void mandel_main(const int thread_no)
{
  int t = 0, ready = 0;
  int threads = thread_no * thread_no;
  int msc = 64 / thread_no;
  TaskHandle_t th;
  tparam_t *tp[64];
  if (thread_no > 8)
  {
    Serial.println("too many threads... giving up.");
    return;
  }
  SemaphoreHandle_t s = xSemaphoreCreateCounting(thread_no, 0);
  vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);

  dma_display.flipDMABuffer();
  for (int tx = 0; tx < thread_no; tx++)
  {
    for (int ty = 0; ty < thread_no; ty++)
    {
      tp[t] = new tparam_t(t, tx * msc, tx * msc + msc - 1,
                           ty * msc, ty * msc + msc - 1,
                           s);
      xTaskCreatePinnedToCore(mandel_wrapper, "TMandel", 1000, tp[t], configMAX_PRIORITIES - 2, &th, t % 2);
      t++;
    }
  }
  Serial.print("MainThread waiting for mandelthreads...");
  for (ready = 0; ready < threads; ready++)
  {
    xSemaphoreTake(s, portMAX_DELAY);
  }
  Serial.println("all threads done.");
  dma_display.showDMABuffer();
  dma_display.flipDMABuffer();
}

void setup()
{
  Serial.begin(115200);

  Serial.println("*****************************************************");
  Serial.println(" HELLO !");
  Serial.println("*****************************************************");

  for (int i = 0; i < 80; i++)
  {
    ct[1 + i] = rgb_24(15 + i * 3, 0, 0);
    ct[81 + i] = rgb_24(0, 15 + i * 3, 0);
    ct[161 + i] = rgb_24(0, 0, 15 + i * 3);
  }

  dma_display.begin(R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN);

  for (int y = 0; y < dma_display.height(); y++)
  {
    dma_display.drawFastHLine(0, y - 1, dma_display.width(), dma_display.color565(255 - (15 * y), 0, 0));
    delay(10);
  }

  delay(1000);

  for (int y = 0; y < dma_display.height(); y++)
  {
    dma_display.drawFastHLine(0, y - 1, dma_display.width(), dma_display.color565(128, 255 - (15 * y), 0));
    delay(10);
  }

  delay(1000);

  // draw a pixel in solid white
  dma_display.drawPixel(0, 0, dma_display.color444(15, 15, 15));
  delay(500);

  // fix the screen with green
  dma_display.fillRect(0, 0, dma_display.width(), dma_display.height(), dma_display.color444(0, 15, 0));
  delay(500);

  // draw a box in yellow
  dma_display.drawRect(0, 0, dma_display.width(), dma_display.height(), dma_display.color444(15, 15, 0));
  delay(500);

  // draw an 'X' in red
  dma_display.drawLine(0, 0, dma_display.width() - 1, dma_display.height() - 1, dma_display.color444(15, 0, 0));
  dma_display.drawLine(dma_display.width() - 1, 0, 0, dma_display.height() - 1, dma_display.color444(15, 0, 0));
  delay(500);

  // draw a blue circle
  dma_display.drawCircle(10, 10, 10, dma_display.color444(0, 0, 15));
  delay(500);

  // fill a violet circle
  dma_display.fillCircle(40, 21, 10, dma_display.color444(15, 0, 15));
  delay(500);

  // fill the screen with 'black'
  dma_display.fillScreen(dma_display.color444(0, 0, 0));

  // draw some text!
  dma_display.setTextSize(1);     // size 1 == 8 pixels high
  dma_display.setTextWrap(false); // Don't wrap at end of line - will do ourselves

  dma_display.setCursor(5, 0); // start at top left, with 8 pixel of spacing
  uint8_t w = 0;
  const char *str = "ESP32 DMA";
  for (w = 0; w < 9; w++)
  {
    dma_display.setTextColor(dma_display.color565(255, 0, 255));
    dma_display.print(str[w]);
  }

  dma_display.println();
  for (w = 9; w < 18; w++)
  {
    dma_display.setTextColor(16384);
    dma_display.print(str[w]);
  }
  dma_display.println();
  //dma_display.setTextColor(dma_display.Color333(4,4,4));
  //dma_display.println("Industries");
  dma_display.setTextColor(dma_display.color444(15, 15, 15));
  dma_display.println("LED MATRIX!");

  // print each letter with a rainbow color
  dma_display.setTextColor(dma_display.color444(0, 8, 15));
  dma_display.print('3');
  dma_display.setTextColor(dma_display.color444(15, 4, 0));
  dma_display.print('2');
  dma_display.setTextColor(dma_display.color444(15, 15, 0));
  dma_display.print('x');
  dma_display.setTextColor(dma_display.color444(8, 15, 0));
  dma_display.print('6');
  dma_display.setTextColor(dma_display.color444(8, 0, 15));
  dma_display.print('4');

  // Jump a half character
  dma_display.setCursor(34, 24);
  dma_display.setTextColor(dma_display.color444(0, 15, 15));
  dma_display.print("*");
  dma_display.setTextColor(dma_display.color444(15, 0, 0));
  dma_display.print('R');
  dma_display.setTextColor(dma_display.color444(0, 15, 0));
  dma_display.print('G');
  dma_display.setTextColor(dma_display.color444(0, 0, 15));
  dma_display.print("B");
  dma_display.setTextColor(dma_display.color444(15, 0, 8));
  dma_display.println("*");

  delay(2000);

  for (int i = 255; i > 0; i -= 2)
  {
    // fade out
    dma_display.fillScreen(dma_display.color565(0, 0, i));
    delay(10);
  }
  for (int i = 255; i > 0; i -= 2)
  {
    // fade out
    dma_display.fillScreen(dma_display.color565(0, i, 0));
    delay(10);
  }
  for (int i = 255; i > 0; i -= 2)
  {
    // fade out
    dma_display.fillScreen(dma_display.color565(i, 0, 0));
    delay(10);
  }

  for (int i = 15; i > 0; i--)
  {
    // draw a blue circle
    dma_display.drawCircle(10, 10, 10, dma_display.color565(i, 0, i * 17));
    delay(250);
  }
  mandel_main(8);
}

void loop()
{
  // do nothing
}

// Input a value 0 to 24 to get a color value.
// The colours are a transition r - g - b - back to r.
uint16_t Wheel(byte WheelPos)
{
  if (WheelPos < 8)
  {
    return dma_display.color444(15 - WheelPos * 2, WheelPos * 2, 0);
  }
  else if (WheelPos < 16)
  {
    WheelPos -= 8;
    return dma_display.color444(0, 15 - WheelPos * 2, WheelPos * 2);
  }
  else
  {
    WheelPos -= 16;
    return dma_display.color444(0, WheelPos * 2, 7 - WheelPos * 2);
  }
}
