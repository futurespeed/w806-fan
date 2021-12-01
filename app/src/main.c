#include <stdio.h>
#include "wm_hal.h"
#include "font.h"
#include "icon.h"

#define CH_PWM_FAN ((uint32_t)PWM_CHANNEL_0)

#define PIN_FAN ((uint32_t)GPIO_PIN_15)

#define PIN_SM_1 ((uint32_t)GPIO_PIN_11)
#define PIN_SM_2 ((uint32_t)GPIO_PIN_12)
#define PIN_SM_3 ((uint32_t)GPIO_PIN_13)
#define PIN_SM_4 ((uint32_t)GPIO_PIN_14)

#define PIN_BTN_OP ((uint32_t)GPIO_PIN_6)
#define PIN_BTN_UP ((uint32_t)GPIO_PIN_7)
#define PIN_BTN_DOWN ((uint32_t)GPIO_PIN_8)

#define PIN_SPI_RST ((uint32_t)GPIO_PIN_9)
#define PIN_SPI_DC ((uint32_t)GPIO_PIN_10)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum STAT
{
	STAT_OFF = 0,
	STAT_ON = 1
};

enum BTN_STAT
{
	BTN_STAT_RELEASE = 0,
	BTN_STAT_PRESS = 1,
	BTN_STAT_PRESS_LONG = 2
};

enum OP_MODE
{
	OP_MODE_INFO = 0,
	OP_MODE_FAN,
	OP_MODE_SWING
};

enum DIRECTION
{
	DIR_FOWARD = 0,
	DIR_BACKWARD = 1
};

struct btn_info
{
	uint32_t pin;
	volatile int hc;
	volatile int lc;
	volatile uint8_t state;
};

PWM_HandleTypeDef hpwm;
SPI_HandleTypeDef hspi;
void Error_Handler(void);
static void GPIO_Init(void);
static void PWM_Init(void);
static void SPI_Init(void);
static void BTN_Init(void);
static void OLED_Init(void);
static void spi_write(uint8_t data[], int size, uint8_t dc);
static void spi_write_cmd(uint8_t data[], int size);
static void spi_write_data(uint8_t data[], int size);
static void oled_point(int x, int y, uint8_t fill);
static void oled_fill(uint8_t fill);
static void oled_put_char(int x, int y, char *str, uint8_t fill, int scale);
static void oled_put_img(int x, int y, struct img_info img, uint8_t fill);

static void btn_lc_incr(struct btn_info *btn);
static void btn_hc_incr(struct btn_info *btn);
static void btn_scan(void);
static void btn_trig(struct btn_info *btn, uint8_t btn_state);
static void btn_op_press(uint8_t btn_state);
static void btn_up_press(uint8_t btn_state);
static void btn_down_press(uint8_t btn_state);
static void fan_speed_update(void);
static void fan_power_toggle(void);
static void next_op_mode(void);
static void go_home(void);
static void op_mode_switch(void);
static void fan_scan(void);
static void swing_scan(void);
static void swing_toggle(void);
static void display_change(void);
static void display_scan(void);
static void display_update(void);

static struct font_info *default_font = &font2;
static struct btn_info btn_op;
static struct btn_info btn_up;
static struct btn_info btn_down;
static struct btn_info *btn_infos[] = {&btn_op, &btn_up, &btn_down};
static int btn_size = ARRAY_SIZE(btn_infos);

static uint8_t op_modes[] = {OP_MODE_INFO, OP_MODE_FAN, OP_MODE_SWING};
static int op_mode_size =  ARRAY_SIZE(op_modes);
static volatile uint8_t op_mode;

static volatile int fan_speed = 49;//0~99
static volatile int fan_speed_step = 10;
static volatile int fan_speed_run_step = 0;
static volatile uint8_t fan_state = STAT_OFF;

static volatile uint8_t swing_state = STAT_OFF;
static volatile int swing_degree = 120;
static volatile int swing_remain_step = 0;
static volatile int swing_pause_step = 0;
static volatile uint8_t swing_dir = DIR_FOWARD;
static volatile int swing_motor_step = 0;
static volatile int swing_motor_step_count = 0;

static uint8_t OLED_GRAM[128][8];
static volatile uint8_t oled_need_refresh = 0;

static void GPIO_Init(void)
{
	printf("gpio init...\n");
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	
	__HAL_RCC_GPIO_CLK_ENABLE();

	
	GPIO_InitStruct.Pin = PIN_FAN | PIN_SM_1 | PIN_SM_2 | PIN_SM_3 | PIN_SM_4| PIN_SPI_RST | PIN_SPI_DC;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	HAL_GPIO_WritePin(GPIOB, PIN_SM_1 | PIN_SM_2 | PIN_SM_3 | PIN_SM_4 | PIN_SPI_RST | PIN_SPI_DC, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOB, PIN_FAN, GPIO_PIN_RESET);
	
	GPIO_InitStruct.Pin = PIN_BTN_OP | PIN_BTN_UP | PIN_BTN_DOWN;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	
	//HAL_NVIC_SetPriority(GPIOB_IRQn, 0);
	//HAL_NVIC_EnableIRQ(GPIOB_IRQn);

}

static void PWM_Init(void)
{
	printf("pwm init...\n");
	// 输出200KHz、占空比40%的波形
	hpwm.Instance = PWM;
	hpwm.Init.AutoReloadPreload = PWM_AUTORELOAD_PRELOAD_ENABLE;
	hpwm.Init.CounterMode = PWM_COUNTERMODE_EDGEALIGNED_DOWN;
	hpwm.Init.Prescaler = 4;
	hpwm.Init.Period = 99;	// 40M / 4 / 100K - 1
	hpwm.Init.Pulse = 19;	// 20% DUTY
	hpwm.Init.OutMode = PWM_OUT_MODE_INDEPENDENT;
	hpwm.Channel = CH_PWM_FAN;
	
	HAL_PWM_Init(&hpwm);
}

static void SPI_Init(void)
{
	printf("spi init...\n");
	hspi.Instance = SPI;
	hspi.Init.Mode = SPI_MODE_MASTER;
	hspi.Init.CLKPolarity = SPI_POLARITY_LOW;
	hspi.Init.CLKPhase = SPI_PHASE_1EDGE;
	hspi.Init.NSS = SPI_NSS_SOFT;
	hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_20;
	hspi.Init.FirstByte = SPI_LITTLEENDIAN;
	
	if (HAL_SPI_Init(&hspi) != HAL_OK)
	{
		Error_Handler();
	}
}

static void BTN_Init(void)
{
	printf("btn init...\n");
	btn_op.pin = PIN_BTN_OP;
	btn_up.pin = PIN_BTN_UP;
	btn_down.pin = PIN_BTN_DOWN;
	for(int i = 0; i < btn_size; i++)
	{
		btn_infos[i]->lc = 0;
		btn_infos[i]->hc = 0;
		btn_infos[i]->state = BTN_STAT_RELEASE;
	}
}

static void spi_write(uint8_t data[], int size, uint8_t dc)
{
	HAL_GPIO_WritePin(GPIOB, PIN_SPI_DC, dc);
	//__HAL_SPI_SET_CS_LOW(&hspi);
	HAL_SPI_Transmit(&hspi, data, size, 1000);
	//__HAL_SPI_SET_CS_HIGH(&hspi);
}

static void spi_write_cmd(uint8_t data[], int size)
{
	spi_write(data, size, GPIO_PIN_RESET);
}

static void spi_write_data(uint8_t data[], int size)
{
	spi_write(data, size, GPIO_PIN_SET);
}

static void OLED_Init(void)
{
	printf("oled init...\n");
	SPI_Init();

	HAL_GPIO_WritePin(GPIOB, PIN_SPI_RST, GPIO_PIN_SET);
	HAL_Delay(100);
	HAL_GPIO_WritePin(GPIOB, PIN_SPI_RST, GPIO_PIN_RESET);
	HAL_Delay(100);
	HAL_GPIO_WritePin(GPIOB, PIN_SPI_RST, GPIO_PIN_SET);
	HAL_Delay(100);

	uint8_t oled_init_data[] = {
		0xAE,//display off
		0x00,//set lower column address
		0x10,//set higher column address
		0x40,//set display start line
		0xB0,//set page address
		0x81,0x66,//set contrast control
		0xA1,//set segment re-map
		0xA6,//set normal/inverse display
		0xA8,0x3F,//set multiplex ratio, duty=1/64
		0xC8,//set com output scan direction
		0xD3,0x00,//set display offset
		0xD5,0x80,//set display clock divide ratio/oscilator frequency
		0xD9,0x1F,//set pre-charge period
		0xDA,0x12,//set com pins hardware configuration
    	0xDB,0x30,//set vcomh deselect level
		0xA4,//set entire display on/off
		0x8D,0x14,//set charge pump
		0xAF//set display on
	};
	spi_write_cmd(oled_init_data, sizeof(oled_init_data));

	HAL_Delay(100);
	// TODO
}

static void oled_point(int x, int y, uint8_t fill)
{
	if(x>127||y>63) return;

    int pos = y/8;
    int bx = y%8;
    uint8_t t = 1<<bx;

    if(fill) OLED_GRAM[x][pos]|=t;
    else OLED_GRAM[x][pos]&=~t;

	oled_need_refresh = 1;
}
static void oled_fill(uint8_t fill){
	for(int i = 0, j = 0; i < 128; i++)
		for(j = 0; j < 8; j++)
			OLED_GRAM[i][j] = fill & 0x01;
}
static void oled_put_char(int x, int y, char *str, uint8_t fill, int scale)
{
	int str_idx, x_idx, y_idx;
    uint8_t c;
    int len = strlen(str);
    int font_width = default_font->width;
    int font_height = default_font->height;
    int spacing = default_font->spacing;
    int offset = default_font->offset;
    for(str_idx = 0; str_idx < len; str_idx++)
    {
        c = (uint8_t)str[str_idx];
        for(x_idx = 0; x_idx < font_width*scale; x_idx++)
            for(y_idx = 0; y_idx < font_height*scale; y_idx++)
                if(default_font->data[(c-offset)*font_width+x_idx/scale]>>y_idx/scale & 0x01)
                    oled_point(x+x_idx + (font_width+spacing)*scale*str_idx, y+y_idx, fill);
                else
					oled_point(x+x_idx + (font_width+spacing)*scale*str_idx, y+y_idx, ~fill&0x01);
    }
}

static void oled_put_img(int x, int y, struct img_info img, uint8_t fill)
{
	int i,j;
	for(j = 0; j < img.height; j++)
		for(i = 0; i < img.width; i++)
			if(img.data[j*img.width + i])
				oled_point(i + x, j + y, fill);
}


static void btn_lc_incr(struct btn_info *btn)
{
	btn->lc++;
	if(btn->lc > 10000) btn->lc = 10000;
}

static void btn_hc_incr(struct btn_info *btn)
{
	btn->hc++;
	if(btn->hc > 10000) btn->hc = 10000;
}

static void btn_scan(void)
{
	// check every 10ms
	struct btn_info *btn;
	for(int i = 0; i < btn_size; i++)
	{
		btn = btn_infos[i];
		if (GPIO_PIN_RESET == HAL_GPIO_ReadPin(GPIOB, btn->pin))
		{
			btn_lc_incr(btn);
			btn->hc = 0;
		} else {
			if(btn->lc > 5)
			{
				if(btn->lc > 80)
				{
					btn_trig(btn, BTN_STAT_PRESS_LONG);
				}else{
					btn_trig(btn, BTN_STAT_PRESS);
				}
			}
			btn_hc_incr(btn);
			if(btn->hc > 5)
			{
				btn->state = BTN_STAT_RELEASE;
				btn->lc = 0;
			}
		}
	}
}

static void btn_trig(struct btn_info *btn, uint8_t btn_state)
{
	if(btn_state == btn->state) return;
	btn->state = btn_state;

	printf("btn pin [%d] press, state: %d\n", btn->pin, btn_state);
	

	if(PIN_BTN_OP == btn->pin)
		btn_op_press(btn_state);
	if(PIN_BTN_UP == btn->pin)
		btn_up_press(btn_state);
	if(PIN_BTN_DOWN == btn->pin)
		btn_down_press(btn_state);

}

static void btn_op_press(uint8_t btn_state)
{
	if(BTN_STAT_PRESS == btn_state)
	{
		if(OP_MODE_INFO == op_mode)
		{
			fan_power_toggle();
		}else{
			next_op_mode();
		}
	}
	if(BTN_STAT_PRESS_LONG == btn_state)
	{
		if(OP_MODE_INFO == op_mode)
		{
			next_op_mode();
		}else{
			go_home();
		}
	}
	display_change();
}

static void btn_up_press(uint8_t btn_state)
{
	if(OP_MODE_FAN == op_mode)
	{
		if(fan_speed + fan_speed_step >= 99)
			fan_speed = 99;
		else
			fan_speed += fan_speed_step;
		fan_speed_update();
	}
	if(OP_MODE_SWING == op_mode)
	{
		swing_toggle();
	}
	display_change();
	// TODO
}

static void btn_down_press(uint8_t btn_state)
{
	if(OP_MODE_FAN == op_mode)
	{
		if(fan_speed - fan_speed_step <= 0)
			return;
		else
			fan_speed -= fan_speed_step;
		fan_speed_update();
	}
	if(OP_MODE_SWING == op_mode)
	{
		swing_toggle();
	}
	display_change();
	// TODO
}

static void fan_speed_update(void)
{	
	if(STAT_OFF == fan_state) return;
//	HAL_PWM_Duty_Set(&hpwm, CH_PWM_FAN, fan_speed);
	printf("fan speed: %d\n", fan_speed);
}

static void fan_power_toggle(void)
{
	if(STAT_OFF == fan_state)
	{
		HAL_GPIO_WritePin(GPIOB, PIN_FAN, GPIO_PIN_SET);
//		HAL_PWM_Duty_Set(&hpwm, CH_PWM_FAN, fan_speed);
	} else {
		HAL_GPIO_WritePin(GPIOB, PIN_FAN, GPIO_PIN_RESET);
//		HAL_PWM_Duty_Set(&hpwm, CH_PWM_FAN, 0);
	}
	fan_state = ~fan_state & 0x01;
	printf("fan state: %d\n", fan_state);
}

static void next_op_mode(void)
{
	if(op_mode == op_mode_size - 1)
	{
		op_mode = 0;
	}else{
		op_mode++;
	}
	op_mode_switch();
}

static void go_home(void)
{
	printf("go home\n");
	op_mode = 0;
	op_mode_switch();
}

static void op_mode_switch(void)
{
	printf("op mode swift to [%d]\n", op_mode);
}

static void fan_scan(void){
	if(STAT_OFF == fan_state){
		HAL_GPIO_WritePin(GPIOB, PIN_FAN, GPIO_PIN_RESET);
		return;
	}
	fan_speed_run_step = (fan_speed_run_step + 1) % 10;
	if(fan_speed_run_step <= fan_speed/10){
		HAL_GPIO_WritePin(GPIOB, PIN_FAN, GPIO_PIN_SET);
	}else{
		HAL_GPIO_WritePin(GPIOB, PIN_FAN, GPIO_PIN_RESET);
	}
	// TODO
}

static void swing_scan(void)
{
	// run stepmotor every 50ms
	if(STAT_OFF == swing_state) return;
	swing_motor_step_count = (swing_motor_step_count + 1) % 5;
	if(swing_motor_step_count != 1) return;
	if(swing_remain_step <= 0) return;
	if(DIR_FOWARD == swing_dir){
		if(0 == swing_motor_step){
			HAL_GPIO_WritePin(GPIOB, PIN_SM_1, GPIO_PIN_SET);
			HAL_GPIO_WritePin(GPIOB, PIN_SM_2|PIN_SM_3|PIN_SM_4, GPIO_PIN_RESET);
		}else if(1 == swing_motor_step){
			HAL_GPIO_WritePin(GPIOB, PIN_SM_3, GPIO_PIN_SET);
			HAL_GPIO_WritePin(GPIOB, PIN_SM_1|PIN_SM_2|PIN_SM_4, GPIO_PIN_RESET);
		}
	}else if(DIR_BACKWARD == swing_dir){
		if(0 == swing_motor_step){
			HAL_GPIO_WritePin(GPIOB, PIN_SM_2, GPIO_PIN_SET);
			HAL_GPIO_WritePin(GPIOB, PIN_SM_1|PIN_SM_3|PIN_SM_4, GPIO_PIN_RESET);
		}else if(1 == swing_motor_step){
			HAL_GPIO_WritePin(GPIOB, PIN_SM_4, GPIO_PIN_SET);
			HAL_GPIO_WritePin(GPIOB, PIN_SM_1|PIN_SM_2|PIN_SM_3, GPIO_PIN_RESET);
		}
	}
	swing_motor_step = (swing_motor_step + 1) % 2; // 2 step loop
	swing_remain_step--;

	// flip
	if (swing_remain_step<=0){
		swing_dir = ~swing_dir & 0x01;
		swing_remain_step = (int) 51200/360*swing_degree/100;
	}
}

static void swing_toggle(void)
{
	if(STAT_OFF == swing_state){
		if(0 == swing_pause_step)
		{
			swing_pause_step = (int) 51200/360*swing_degree/100/2;
			printf("pause step: %d\n", swing_pause_step);
		}
		swing_remain_step = swing_pause_step;
	}else{
		swing_pause_step = swing_remain_step;
		swing_remain_step = 0;
		// set motor idle
		HAL_GPIO_WritePin(GPIOB, PIN_SM_1|PIN_SM_2|PIN_SM_3|PIN_SM_4, GPIO_PIN_SET);
	}
	swing_state = ~swing_state & 0x01;
	printf("swing state: %d, remain step: %d\n", swing_state, swing_remain_step);
}

static void display_change(void)
{
	oled_fill(0);
	if(OP_MODE_INFO == op_mode)
	{
		oled_put_img(17, 11, fan_ico, 1);
		char str[20];
		sprintf(str, "Fan %s", STAT_ON == fan_state ? "ON" : "OFF");
		oled_put_char(71, 26, str, 1, 1);
	}
	if(OP_MODE_FAN == op_mode)
	{
		oled_put_char(16, 9, "Fan Speed", 1, 1);
		char str[20];
		sprintf(str, "%d", fan_speed + 1);
		oled_put_char(53, 30, str, 1, 2);
	}
	if(OP_MODE_SWING == op_mode)
	{
		oled_put_char(16, 9, "Swing", 1, 1);
		char str[20];
		sprintf(str, "%s", STAT_ON == swing_state ? "ON" : "OFF");
		oled_put_char(53, 30, str, 1, 2);
	}
}

static void display_scan(void)
{
	if(oled_need_refresh != 0)
	{
		display_update();
		oled_need_refresh = 0;
	}
}

static void display_update(void)
{
	// tranfer GRAM data to OLED
	for(int i = 0, j = 0; i < 8; i++)
	{
		//printf("write page %d\n", i);
		// set page
		uint8_t page_data[] = {0xB0+i, 0x00, 0x10};
		spi_write_cmd(page_data, sizeof(page_data));

		// transfer data
		uint8_t tx[128];
		for(j = 0; j < 128; j++)
            tx[j] = OLED_GRAM[j][i];
		spi_write_data(tx, sizeof(tx));
	}
}

void HAL_GPIO_EXTI_Callback(GPIO_TypeDef *GPIOx, uint32_t GPIO_Pin)
{
	if ((GPIOx == GPIOB) && (GPIO_Pin == GPIO_PIN_5))
	{
		// TODO
	}
}

void Error_Handler(void)
{
	printf("error handler...\n");
	while (1)
	{
	}
}

void assert_failed(uint8_t *file, uint32_t line)
{
	printf("Wrong parameters value: file %s on line %d\r\n", file, line);
}

int main(void)
{
	SystemClock_Config(CPU_CLK_160M);
	printf("enter main [FS]\r\n");

	HAL_Init();
	GPIO_Init();
//	PWM_Init();
	BTN_Init();
	OLED_Init();

//	HAL_PWM_Start(&hpwm, CH_PWM_FAN);
//	HAL_PWM_Duty_Set(&hpwm, CH_PWM_FAN, 0);

	op_mode = op_modes[0];
	display_change();
	
	printf("start scan...\n");
	while (1)
	{
		btn_scan();
		fan_scan();
		swing_scan();
		display_scan();

		HAL_Delay(10);
	}
}

