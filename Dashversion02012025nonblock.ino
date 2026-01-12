#include <Preferences.h>
#include <Waveshare_ST7262_LVGL.h>
#include <lvgl.h>
#include <lv_conf.h>
#include "ui.h"
#include "driver/twai.h"
#define RX_PIN 19  
#define TX_PIN 20 
#define POLLING_RATE_MS 100 

int interval_can = 50;
int interval_screenchange = 250;
unsigned long startTime_can;
unsigned long startTime_screenchange;

int Idle;
int32_t rpm ;
int32_t  rearwheelspeed;
int8_t TPS;
int8_t APS;
int8_t gear ;
int16_t EngineTemp ;
int16_t BatteryV;
int16_t AmbientTemp;
int8_t fuel ;
int16_t  MAPV ;
int16_t  MAPH ;
int OxyV ;
int OxyH ;
int32_t speed ;
int Indic;
int LHindicat =0;
int RHindicat =0;
int Brake = 0;
uint32_t  ODO ;
uint32_t  ODOprevious ;
uint32_t  Kilometer;
int ClockMins ;
int ClockHrs ;
uint32_t  TotalTrip;
uint32_t   Trip;
int KEYSense  ;
uint16_t KEYnumber ;
static bool driver_installed = false;
char buf[16]; //is it large enough for 15000
int isOn = 1;  //1 is backlight
Preferences preferences;
//end setup


void setup()
{
    Serial.begin(115200); 

    preferences.begin("SETTINGS", false); 
    

  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)TX_PIN, (gpio_num_t)RX_PIN, TWAI_MODE_LISTEN_ONLY);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();  //Look in the api-reference for other speed sets.
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL(); 
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK)
     { 
    Serial.println("Driver installed");
     }
     else
    {
    Serial.println("Failed to install driver");
    return;
    }
     driver_installed = true;      

    if (twai_start() == ESP_OK)
     {
    Serial.println("Driver started");
    } 
    else 
    {
    Serial.println("Failed to start driver");
    return;
    }
     startTime_can = millis();
     startTime_screenchange = millis();

        lcd_init();   
        lvgl_port_lock(-1);    
        ui_init();
        lvgl_port_unlock();
        delay(5000);//
}
//**************************************************************************************************
void loop() { 
unsigned long currentTime_can = millis();

  if(currentTime_can - startTime_can >= interval_can)
  { 
     startTime_can = currentTime_can;
     twai_start();
     twai_message_t message;
  
      if(twai_receive(&message, pdMS_TO_TICKS(10000)) == ESP_OK) //????????Blocking????????
       {
          if (message.identifier == 0x18)
          {
            gear = ((message.data[4]>>4)/2);
            rearwheelspeed =  ((((message.data[4]&0xf)*256) + message.data[5])*0.15)/0.6213;//kmperhr
            speed =rearwheelspeed;
            fuel = ((message.data[6])/10);//fuel in litres
          }

        if (message.identifier == 0x20)
          {
           
           Indic =((message.data[1])>>4);
           // Serial.println(Indic);
             if (Indic= 0){ LHindicat =0; Brake = 0;RHindicat=0;}
             if (Indic= 1){ LHindicat =0; Brake = 1;RHindicat=0;}
             if (Indic= 4){ LHindicat =1; Brake = 0;RHindicat=0;}
             if (Indic= 5){ LHindicat =1; Brake = 1;RHindicat=0;}
             if (Indic= 8){ LHindicat =0; Brake = 0;RHindicat=1;}
             if (Indic= 9){ LHindicat =0; Brake = 1;RHindicat=1;}
      
       
            KEYnumber = (message.data[3]+message.data[4]+message.data[5]) ;  //no key =0, key present = 522 , HEX(4C:EE:D0)
            if (KEYnumber != 522){
              KEYSense = 0;
            }
            else KEYSense = 522;
          }

         if (message.identifier == 0x80)
          {
            TPS = ((message.data[1])/2);  
            APS=((message.data[0])/2);//computer determined tps
            rpm = (message.data[5])*256 +(message.data[6]);    
          }

          if (message.identifier == 0x100)
          { 
           EngineTemp = message.data[3]-40;
           BatteryV = message.data[4];
           AmbientTemp =message.data[5]-40;
          }

          if (message.identifier == 0x150)
          {
            //Idle = message.data[0];
          MAPV = (((message.data[6]&0xf))*256)+message.data[7];     
          OxyV= message.data[1];              
          OxyH = message.data[2];         
          } 

          if (message.identifier == 0x160)
          {
          //message.data[2]; TICKS ON OFF SWITCH
          //message.data[3]; TICKS ON OFF SWITCH follows [2]
          MAPH = (((message.data[6]&0xf))*256)+message.data[7];
          }

          if (message.identifier == 0x300)
          {
            Kilometer   = preferences.getLong("Kilometer", Kilometer );
            ODOprevious = preferences.getLong("ODOprevious" , ODOprevious );
            ODO = ((message.data[5]));//counts to 0-253 in 1km steps the resets to zero????
            if(ODO < ODOprevious)
            {              
              ODOprevious=0;
            }
            else
            Trip = ODO - ODOprevious;
            ODOprevious = ODO;
            Kilometer += Trip;
            preferences.putLong("Kilometer", Kilometer ); 
            preferences.putLong("ODOprevious" , ODOprevious ); 
                
            //EngMode = message.data[6]&0xf;    //////%16;
            ClockMins =(message.data[3]/2);
            ClockHrs = (message.data[6]>>4);   ///%16)/2);   *****1st data[6] >>4****2nd Data[6]&0xf*****
          }         
       }
  }  // end can update task


 unsigned long currentTime_screenchange = millis(); 

if(currentTime_screenchange - startTime_screenchange >= interval_screenchange)
{ 
     startTime_screenchange = currentTime_screenchange;
  //-----------------------------------------ANimate the dash---------------------------------------------------------------------
  lv_bar_set_value(ui_fuellevelbar, fuel, LV_ANIM_ON); 
  sprintf(buf,"%2d",fuel);
   lv_label_set_text(ui_fuelevelnumber, buf);
  //*****************************************************************       
  lv_bar_set_value(ui_RPMbar, rpm, LV_ANIM_ON); 
  sprintf(buf,"%6d",rpm);
   lv_label_set_text(ui_rpmNumber, buf);
  //*****************************************************************
  if (gear != 0)
  {
     sprintf(buf,"%1d",gear);
  lv_label_set_text(ui_GearNumber, buf); 
  }
    else
      {
        lv_label_set_text(ui_GearNumber, "N"); 
      }
  //***************************************************************
   sprintf(buf,"%3d",speed);
  lv_label_set_text(ui_Speednumber, buf);
  //***************************************************************  
   sprintf(buf,"%3d",EngineTemp);
   lv_label_set_text(ui_engtempnumber, buf);
    //*****************************************************************  
   sprintf(buf,"%2d",AmbientTemp);
   lv_label_set_text(ui_Airtempnumber, buf);
  //*****************************************************************   
   sprintf(buf,"%2d",ClockMins);
   lv_label_set_text(ui_Timenumbermins, buf);
  //*****************************************************************   
   sprintf(buf,"%2d",ClockHrs);
   lv_label_set_text(ui_Timenumberhr, buf);
    //*****************************************************************   
   sprintf(buf,"%4d",MAPH);
   lv_label_set_text(ui_mapHnum, buf);
     lv_bar_set_value(ui_MapbarH, MAPH, LV_ANIM_ON); 
  sprintf(buf,"%4d",MAPH);   
  //*****************************************************************   
  sprintf(buf,"%4d",MAPV);
  lv_label_set_text(ui_mapVnum, buf);
  lv_bar_set_value(ui_MapVbar, MAPV, LV_ANIM_ON); 
  sprintf(buf,"%4d",MAPV); 
  //***************************************************************** 
  sprintf(buf,"%3d",APS);
  lv_bar_set_value(ui_APSbar, APS, LV_ANIM_ON); 
  sprintf(buf,"%3d",APS); 
  //*****************************************************************   
  sprintf(buf,"%3d",TPS);
  lv_bar_set_value(ui_TPSbar, TPS, LV_ANIM_ON); 
  sprintf(buf,"%3d",TPS); 
  //***************************************************************** 
   sprintf(buf,"%3d",OxyV);
   lv_label_set_text(ui_oxyVnum, buf);   
  sprintf(buf,"%3d",OxyH);   
  lv_label_set_text(ui_xyHnum, buf);
    //----------------------------------------------------------------
 sprintf(buf,"%4d", BatteryV);   
  lv_label_set_text(ui_Batnum, buf);
  //-------------------------------------------------------------------
 sprintf(buf,"%d", KEYSense);   
  lv_label_set_text(ui_keysense, buf); 
  //----------------------------------------------------------------------
 sprintf(buf,"%7d", Kilometer);   
  lv_label_set_text(ui_Odometer, buf);
}//end screen update


}//end loop

