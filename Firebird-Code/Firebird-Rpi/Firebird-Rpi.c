/*
 * Firebird_Rpi.c
 *
 * Created: 23-04-2015 12:21:18
 *  Author: Saurav
 */ 

#define F_CPU 14745600

#include<avr/io.h>
#include<avr/interrupt.h>
#include<util/delay.h>

unsigned char data;			//to store received data from UDR1
unsigned char ADC_flag; 
unsigned char left_motor_velocity = 0x00;
unsigned char right_motor_velocity = 0x00;

//***************************** Status flags ************
#define IDLE 0
#define _second_last_byte 1
#define _last_byte 2

//*******************************

//************************************Variable Declaration *****************************

unsigned char rec_data = 0;						// single byte received at UDR2 is stored in this variable 
unsigned char uart_data_buff[25] = {0};			// storing uart data in this buffer
unsigned char copy_packet_data[25] = {0};				// storing uart data into another packet data for operation
unsigned char state = IDLE;						// for switch cases in UART ISR
unsigned char end_char_rec = 0; 
unsigned char i = 0 , j = 0; 
unsigned char data_packet_received = 0;			// flag to check if all data_packet is received- goes high when '\!' is received
unsigned char data_copied = 0;					// flag to check if uart_data_buff is copied into packet_data

unsigned char device_id = 0;
unsigned char device_type = 0;
unsigned char function_type = 0;
unsigned char param_count = 0;
unsigned char param_1 = 0, param_2 = 0, param_3 = 0;
//*****************************
void buzzer_pin_config (void)
{
	DDRC = DDRC | 0x08;		//Setting PORTC 3 as outpt
	PORTC = PORTC & 0xF7;		//Setting PORTC 3 logic low to turnoff buzzer
}

void motion_pin_config (void)
{
	DDRA = DDRA | 0x0F;
	PORTA = PORTA & 0xF0;
	DDRL = DDRL | 0x18;   //Setting PL3 and PL4 pins as output for PWM generation
	PORTL = PORTL | 0x18; //PL3 and PL4 pins are for velocity control using PWM.
}

//ADC pin configuration
void adc_pin_config (void)
{
	DDRF = 0x00;  //set PORTF direction as input
	PORTF = 0x00; //set PORTF pins floating
	DDRK = 0x00;  //set PORTK direction as input
	PORTK = 0x00; //set PORTK pins floating
}

//Function to initialize ports
void port_init()
{
	motion_pin_config();
	buzzer_pin_config();
	adc_pin_config();
}

//Function To Initialize UART2 - Rub robot using USB cable
// desired baud rate:9600
// actual baud rate:9600 (error 0.0%)
// char size: 8 bit
// parity: Disabled
void uart2_init(void)
{
	UCSR2B = 0x00; //disable while setting baud rate
	UCSR2A = 0x00;
	UCSR2C = 0x06;
	UBRR2L = 0x5F; //set baud rate lo
	UBRR2H = 0x00; //set baud rate hi
	UCSR2B = 0x98;
}

//ADC initialize
// Conversion time: 56uS
void adc_init(void)
{
	ADCSRA = 0x00;
	ADCSRB = 0x00;		//MUX5 = 0
	ADMUX = 0x20;		//Vref=5V external --- ADLAR=1 --- MUX4:0 = 0000
	ACSR = 0x80;
	ADCSRA = 0x86;		//ADEN=1 --- ADIE=1 --- ADPS2:0 = 1 1 0
}

// Timer 5 initialized in PWM mode for velocity control
// Prescale:256
// PWM 8bit fast, TOP=0x00FF
// Timer Frequency:225.000Hz
void timer5_init()
{
	TCCR5B = 0x00;	//Stop
	TCNT5H = 0xFF;	//Counter higher 8-bit value to which OCR5xH value is compared with
	TCNT5L = 0x01;	//Counter lower 8-bit value to which OCR5xH value is compared with
	OCR5AH = 0x00;	//Output compare register high value for Left Motor
	OCR5AL = 0xFF;	//Output compare register low value for Left Motor
	OCR5BH = 0x00;	//Output compare register high value for Right Motor
	OCR5BL = 0xFF;	//Output compare register low value for Right Motor
	OCR5CH = 0x00;	//Output compare register high value for Motor C1
	OCR5CL = 0xFF;	//Output compare register low value for Motor C1
	TCCR5A = 0xA9;	/*{COM5A1=1, COM5A0=0; COM5B1=1, COM5B0=0; COM5C1=1 COM5C0=0}
 					  For Overriding normal port functionality to OCRnA outputs.
				  	  {WGM51=0, WGM50=1} Along With WGM52 in TCCR5B for Selecting FAST PWM 8-bit Mode*/
	
	TCCR5B = 0x0B;	//WGM12=1; CS12=0, CS11=1, CS10=1 (Prescaler=64)
}

//Function To Initialize all The Devices
void init_devices()
{
	cli(); //Clears the global interrupts
	port_init();  //Initializes all the ports
	uart2_init(); //Initialize UART1 for serial communication
	adc_init(); 
	timer5_init();
	sei();   //Enables the global interrupts
} 

//-------------------------------------------------------------------------------
//-- ADC Conversion Function --------------
//-------------------------------------------------------------------------------
unsigned char ADC_Conversion(unsigned char ch)
{
	unsigned char a;
	if(ch>7)
	{
		ADCSRB = 0x08;
	}
	ch = ch & 0x07;			  //Store only 3 LSB bits
	ADMUX= 0x20 | ch;			  //Select the ADC channel with left adjust select
	ADC_flag = 0x00; 			  //Clear the user defined flag
	ADCSRA = ADCSRA | 0x40;	  //Set start conversion bit
	while((ADCSRA&0x10)==0);	  //Wait for ADC conversion to complete
	a=ADCH;
	ADCSRA = ADCSRA|0x10;        //clear ADIF (ADC Interrupt Flag) by writing 1 to it
	ADCSRB = 0x00;
	return a;
}

// Function for robot velocity control
void velocity (unsigned char left_motor, unsigned char right_motor)
{
	OCR5AL = (unsigned char)left_motor;
	OCR5BL = (unsigned char)right_motor;
}

void motor_enable (void)
{
	PORTL |= 18;		// Enable left and right motor. Used with function where velocity is not used
}
void buzzer_on (void)
{
	PORTC |= 0x08;
}

void buzzer_off (void)
{
	PORTC &= 0xF7;
}

void forward (void)
{
	//PORTA &= 0xF0;
	PORTA = 0x06;
}

void back (void)
{
	//PORTA &= 0xF0;
	PORTA = 0x09;
}

void left (void)
{
	//PORTA &= 0xF0;
	PORTA = 0x05;
}

void right (void)
{
	//PORTA &= 0xF0;
	PORTA = 0x0A;
}

void stop (void)
{
	PORTA = 0x00;
}

//SIGNAL(SIG_USART2_RECV) 		// ISR for receive complete interrupt
ISR(USART2_RX_vect)
{
	rec_data = UDR2; 				//making copy of data from UDR2 in 'data' variable

	while(!(UCSR2A && (1<<RXC2)));	// wait till data byte is received
	
	if (data_packet_received == 0) 
	{
		if (rec_data == '\n' )			// '\n' decimal value is 10
		{
			 //state = _second_last_byte 
			uart_data_buff[i] = rec_data;
			i++;
			end_char_rec = 1;
		//	UDR2 = rec_data;
		}

		else 
		{
			if((end_char_rec == 1) && (rec_data == '\r'))		//'\r' indicates end of transmission. It should come after '\n'
			{
				uart_data_buff[i] = rec_data;
				i++;
				end_char_rec = 2;
				data_packet_received = 1;
				
				for (j = 0;j<i;j++)				// i value is stored in ISR
				{
					copy_packet_data[j] = uart_data_buff[j];
					//UDR2 = copy_packet_data[j];
					uart_data_buff[j] = 0;
				}
			//	UDR2 = rec_data;
			}
	
			else if((end_char_rec == 1) && (rec_data != '\r'))		//'\r' is expected after '\n'. If not received, discard the data. 
			{
			//	UDR2 = 'x';
																	// discard the data and check 
			}
		
			else													// store other data bytes
			{
				uart_data_buff[i] = rec_data;
				i++;
			//	UDR2 = rec_data;
			}
		}
	}
		
/*		if(data == 0x32) //ASCII value of 2
		{
			back(); //back
		}

		if(data == 0x34) //ASCII value of 4
		{
			left();  //left
		}

		if(data == 0x36) //ASCII value of 6
		{
			right(); //right
		}

		if(data == 0x35) //ASCII value of 5
		{
			stop(); //stop
		}

		if(data == 0x37) //ASCII value of 7
		{
			buzzer_on();
		}

		if(data == 0x39) //ASCII value of 9
		{
			buzzer_off();
		}
	
		if (data == 0x11)	// White Line value left
		{
			UDR2 = ADC_Conversion(3);
		}
	
		if (data == 0x12)	// White Line value center
		{
			UDR2 = ADC_Conversion(2);
		}
	
		if (data == 0x13)	// White Line value right
		{
			UDR2 = ADC_Conversion(1);
		}
	
		if (data == 0x14) 
		{
			UDR2 = ADC_Conversion(4);
		}
	
		if (data == 0x50)
		{
			unsigned char flag = 0, right = 0, left = 0;
			//forward();
			while (flag != 2 )
			{
				while (!(UCSR2A & (1<<RXC2)))
				{
					if (flag == 0)
					{
						right = UDR2;
						flag = 1;
					}
				
					else if (flag == 1)
					{
						left = UDR2;
						flag = 2;
					}
				}
				velocity(left,right);
				break;
			}
		} */
	
}	// end of ISR

void send_sensor_data(void)
{
	if (device_id == 0x00)
	{
		UDR2 = ADC_Conversion(0);		// Battery Voltage
	}
	
	if (device_id == 0x01)
	{
		UDR2 = ADC_Conversion(1);		// right WL sensor
	}
	
	if (device_id == 0x02)
	{
		UDR2 = ADC_Conversion(2);		// Center WL sensor
	}
	
	if (device_id == 0x03)
	{
		UDR2 = ADC_Conversion(3);		// left WL sensor
	}
	
	if (device_id == 0x04)
	{
		UDR2 = ADC_Conversion(4);		// IR Proximity sensor-1
	}
	
	if (device_id == 0x05)
	{
		UDR2 = ADC_Conversion(5);		// IR Proximity sensor-2
	}
	
	if (device_id == 0x06)
	{
		UDR2 = ADC_Conversion(6);		// IR Proximity sensor-3
	}
	
	if (device_id == 0x07)
	{
		UDR2 = ADC_Conversion(7);		// IR Proximity sensor-4
	}
	
	if (device_id == 0x08)
	{
		UDR2 = ADC_Conversion(8);		// IR Proximity sensor-5
	}
	
	if (device_id == 0x09)
	{
		UDR2 = ADC_Conversion(9);		// Sharp Sensor-1 
	}

	if (device_id == 0x0A)
	{
		UDR2 = ADC_Conversion(10);		// Sharp Sensor-2
	}

	if (device_id == 0x0B)
	{
		UDR2 = ADC_Conversion(11);		// Sharp Sensor-3
	}

	if (device_id == 0x0C)
	{
		UDR2 = ADC_Conversion(12);		// Sharp Sensor-4
	}

	if (device_id == 0x0D)
	{
		UDR2 = ADC_Conversion(13);		// Sharp Sensor-5
	}

	if (device_id == 0x0E)
	{
		UDR2 = ADC_Conversion(14);		// Connected to servo pod
	}

	if (device_id == 0x0E)
	{
		UDR2 = ADC_Conversion(15);		// Connected to servo pod
	}	
}

void actuate_devices(void)
{
	if (device_id == 0x01)				// Buzzer has device id = 1
	{
		if (function_type == 0x00)
		{
			buzzer_on();
		}
		else if (function_type == 0x01)
		{
			buzzer_off();
		}
	}
	
	if (device_id == 0x02)				// Motor has device id = 2
	{
		if (function_type == 0x00)
		{
			motor_enable();
			forward();
		}
		else if (function_type == 0x01)
		{
			motor_enable();
			back();
		}
		else if (function_type == 0x02)
		{
			motor_enable();
			right();
		}
		else if (function_type == 0x03)
		{
			motor_enable();
			left();
		}
		else if (function_type == 0x04)
		{
			motor_enable();
			stop();
		}
		else if (function_type == 0x09)
		{
			forward();
			UDR2 = param_1;
			velocity(param_1,param_2);
			
		}
	}
}

void decode_data(void)
{
	while (data_copied == 1)
	{
		device_id = copy_packet_data[0];
		device_type = copy_packet_data[1];
		function_type = copy_packet_data[2];
		param_count = copy_packet_data[3];
		param_1 = copy_packet_data[4];
		param_2 = copy_packet_data[5];
		data_copied = 0;
	//	UDR2 = 'D';
	}
	
	if ((data_copied == 0) && (device_type == 0x00))	// input devices such as sensors, which will send back data
	{
		send_sensor_data();
	}
	
	else if ((data_copied == 0) && (device_type == 0x01)) // output devices such as buzzer, motors
	{
	//	UDR2 = 'A';
		actuate_devices();
	}
}

void copy_data_packet()
{
	if (data_packet_received == 1)
	{
		
		//for (j = 0;j<i;j++)				// i value is stored in ISR
		//{
			//copy_packet_data[j] = uart_data_buff[j];
			////UDR2 = copy_packet_data[j];
			//uart_data_buff[j] = 0;
		//}
		i=0;
		j=0;
		data_packet_received = 0;
		end_char_rec = 0;
		data_copied = 1;
		
	//	UDR2 = data_copied;
		decode_data();
		//UDR2 = 'I';
		//_delay_ms(1000);
	}
	//UDR2 = 'O';
	
}



//Main Function
int main(void)
{
	init_devices();
	while(1)
	{
		copy_data_packet();
	}
}