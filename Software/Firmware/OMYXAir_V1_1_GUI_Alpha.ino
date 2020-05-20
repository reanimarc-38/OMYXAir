/***************************************************************************
Copyright 2020 
Xavier Galtier - Marc Pleyber - Orphee Cugat - Mickael Brunello - Yohan Chabanne
FROM ORIGINAL MUR-PROJECT LECLUBSANDWICH
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
***************************************************************************/
//          AVERTISSEMENT UNITE PRESSIONS
// 1 atmosphère = 1013 hecto Pascal = 10 m d'eau = 1000 cm H2O
// 1 hecto Pascal = 1 cm H2O
// les réanimateurs travaillent entre 5 et 40 cm H2O
// nous devons fixer nos consignes en cm H20 donc en hPa = de 5 à 40 hPa
// le capteur renvoie l'info en Volts, sortie max Capteur 5V pour 100 hPa
// (le teensy analog input 3V3 max donc maxi lisible 70 hPa)
#include <Servo.h>
#include <Wire.h>
#include <Arduino.h>

// ou/et comment imprimer les Valeurs  =1 plot_valeur; =2 print_valeur;  =0 interface graphique
#define PRINT_WHERE 0 // 0 GUI,  1 TRACEUR,  2 MONITOR
#define TEENSY 0	  // 1 pour utilisation Teensy,  0 pour Arduino ou compatible

#if TEENSY == 1
//bibliotheque specifique Teensy (a partir de maintenant on peut plus utiliser un autre carte)
#include <ADC.h>
#include <ADC_util.h>
#define ADC_MAX 65535
ADC *adc = new ADC(); // adc object dedans 2 adc:
					  // adc0 pour les capteurs (filtres)
					  // adc1 pour les potards (non filtres +rapide)
					  //--------------------------------------------------------------------------------------------
#else
#define ADC_MAX 1023
#endif

// declaration fonction
void calcul_Data();
char test_Trigger_Inspi();
char test_Trigger_Expi();
void print_Data_GUI();
void print_Valeurs();
void lire_Pression_Sensor();
void gestion_Alarme();
void print_Consigne_Interface_Graphique();
void get_PID_From_Uart();
void process_Consigne();
float calcul_PID(float consigne, float sensor, bool init_val);
void sature_Position_Valve_In(int position);
void sature_Position_Valve_Out(int position);
float mesure_Flow_Venturi();
void offset_Capteurs();
void lire_Potentiometre();
void print_Potentiometre();
void plot_Valeurs();

// Parametre regulation:
float coeff_P = 4;	 // constante proportionnelle
float coeff_I = 5;	 // constante de temps integration
float coeff_D = 0.1; // constante de temps derivee

// definition des constantes
#define VALVE_IN_MAX 100	  //valeur fermeture VALVE VTT
#define VALVE_IN_MIN 0		  // valeur ouverture VALVE VTT
#define VALVE_OUT_MAX 100	  // ouverture VALVE d'expiration (boisseau triangle)
#define VALVE_OUT_MIN 0		  // fermeture VALVE d'expiration (boisseau triangle)
#define RATIO_VALVE_INOUT 1.5 // (VALVE_IN_MAX-VALVE_IN_MIN)/(VALVE_OUT_MAX-VALVE_OUT_MIN)

#define MAP_MPXV5010 7333
#define MAP_MPXV5004 3300

// limites alarmes Pression Diff
#define DELTA_CONSIGNE 1.2 // sueil sur le delta consigne/capteur pour detecter trigger
#define MIN_DIFF_PRESS 3.  // mini  300 Pa = 3 hPa = 3 cmH2O
#define MAX_DIFF_PRESS 40. // maxi 4000 Pa = 40 hPa = 40 cmH2O

#define STATE_IDLE 0				// etat attente
#define STATE_PEAK 1				// cycle en cours et etat = generation peak
#define STATE_INSPIRATION_PLATEAU 2 // cycle/etat = gene plateau inspiratoire
#define STATE_EXPIRATION_PLATEAU 3	// cycle/etat = gene plateau expiratoire

#define CAPTEUR_SAMP_TIME 20
#define POTENTIO_SAMP_TIME 50
#define GUI_SAMP_TIME 100
#define GUI_TELL_STATE 5000
#define MINUTE_MS 60000

// definition variables Globales
Servo inValve;			 // servomoteur entree turbine => patient
Servo outValve;			 // servomoteur sortie patient => room
int servo_in, servo_out; // angles servos
unsigned char is_Regul_Correct = 0;
unsigned char etat = STATE_IDLE; // etat courant de la machine à etats
float pression_Patient_Relative; // pression relative du patient (hPa)

// Potentiometer and Servo pins
#define inValvePin 2	  // Pin for input Valve  => REMISES 2 & 3 COMME MUR
#define outValvePin 3	  // Pin for Output Valve => REMISES 2 & 3 COMME MUR
#define Led 5			  // future neopixel Led for user feedback
#define Buzzer 13		  // Alarm Buzzer
#define btn_Maintenance 7 // sets all valves to 0° for maintaince
#define btn_PressureCal 8 // closes outputValve and opens Input Valve for maximum pressure calibration

#define BUZZER_ON digitalWrite(Buzzer, HIGH)
#define BUZZER_OFF digitalWrite(Buzzer, LOW)

// Lecture input potars valeurs analogiques 0 à 3V3
// => pattes Teensy A0 A1 A2 A3 (A6 = venturi OUT capteur MPXV5010/5004, A7 = pression capteur MPXV5010)
//								A8 = venturi IN capteur MPXV5010/5004
float pot_A0, pot_A1, pot_A2, pot_A3, venturi_out_A6, pression_A7,venturi_in_A8;
float pot_A0_old, pot_A1_old, pot_A2_old, pot_A3_old;
float cycle=10000, ratio=50;
bool change_pot;
float debit_l_m, volume_ml=0,volume_tidal_ml, debit,debit_max; // debit en l/min - volume en ml - debit en m3/s
int venturi_in_offset, venturi_out_offset, pression_offset;

// different timers in our breathing cycle
unsigned long temps_cycle = 3000;	// duree totale cycle (ms)
unsigned long temps_peak = 0;		// duree peak  (ms) PAS IMPLEMENTEE
unsigned long temps_plateauIns = 1500; // duree plateau inspiration (ms)
unsigned long temps_plateauExp = 1500; // duree plateau expiration  (ms)
float consigne_Inspi = 20;			// consigne de départ plateau Inspire (hPa)
float consigne_Expi = 6;			// consigne de départ plateau Exprire (hPa)
unsigned long temps_cycle_reel=0;
unsigned char mode_ventilation = 51;
unsigned char alarme_gui = 0;
float trigger_expi = 0.50;
unsigned int alarme_to_gui=0,alarme_to_gui_old=0; 	// 16 bits pour alarme: 	0= pression> valeur maxiet ou mini -
													//						 	1 = consigne potard ? 
													//							2 = apnee ou respi sauvegarde
String inputString = "";	 // a String to hold incoming data
bool stringComplete = false; // whether the string is complete
unsigned char trigger = 5;
// variable pour loop
unsigned long var_stock_temp_cycle = 0;
unsigned long var_stock_temp_capteur = 0;
unsigned long var_stock_temp_potentio = 0;
unsigned long var_stock_temp_gui = 0, var_stock_temp_volume = 0, now = 0;
unsigned long var_stock_temp_tell_state = 0;
unsigned long var_stock_temp_alarme_gui=0;
unsigned long var_stock_temp_freq=0;
unsigned long var_stock_temp_debit_moy=0;
unsigned char cpt_nb_cycle_min=0;
float consigne,debit_moy=0;
int cptserial = 0;
char buffer[64];
int servo_angle, servo_max;

// sauvegarde valeur PID
float err_int = 0;
float last_erreur = 0;
unsigned long last_time = 0;
unsigned char apnee=0;
// MAIN LOOP
void setup()
{
	pinMode(btn_Maintenance, INPUT_PULLUP); // configure entree Bouton
	pinMode(btn_PressureCal, INPUT_PULLUP);
	pinMode(LED_BUILTIN, OUTPUT); // configure Voyant
	pinMode(Buzzer, OUTPUT);	  // configure Buzzer
	BUZZER_OFF;					  // Chut j'ai dit
	Serial.begin(115200);		  // initialisation port serie (utile pour comm: data + courbe )

	pinMode(A0, INPUT);
	pinMode(A1, INPUT);
	pinMode(A2, INPUT);
	pinMode(A3, INPUT);
	pinMode(A6, INPUT);
	pinMode(A7, INPUT);
	// CONFIG Specifique ADC Teensy
#if TEENSY == 1
	adc->adc0->setAveraging(16);											// set number of averages
	adc->adc0->setResolution(16);											// set bits of resolution
	adc->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED_16BITS); // change the conversion speed
	adc->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);				// change the sampling speed
																			//	adc->adc1->setAveraging(1); // set number of averages
																			//    adc->adc1->setResolution(10); // set bits of resolution
																			//	adc->adc1->setConversionSpeed(ADC_CONVERSION_SPEED::HIGH_SPEED_16BITS); // change the conversion speed
																			//	adc->adc1->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED); // change the sampling speed
#endif

	inValve.attach(inValvePin);
	outValve.attach(outValvePin);
	// au démarrage on tripote les vannes pour
	// garantir pression NULLE au patient ET pour calibrer capteurs:
	outValve.write(VALVE_OUT_MIN); // valve OUT OPEN
	delay(300);
	inValve.write(VALVE_IN_MAX); // valve IN CLOSED
	var_stock_temp_volume = 0;
	etat = STATE_IDLE;
	apnee=0;
	//ROUTINE CALIBRATION OFFSET
	offset_Capteurs();
	delay(100);
}

void loop()
{
// GESTION ALARME, JE TE CASSE PAS LES OREILLES
	gestion_Alarme();
// Calcul debit minute moyenne
	if (mode_ventilation<3)
	{
		if (MINUTE_MS < (millis() - var_stock_temp_debit_moy))
		{
			debit_moy=debit_moy/(float)cpt_nb_cycle_min;
			Serial.print("!M");
			Serial.print(debit_moy);
			Serial.println('*');
			debit_moy=0;
			cpt_nb_cycle_min=0;
			var_stock_temp_debit_moy=millis();
		}
	}	
// Lire capteur a intervalles reguliers et Calcul donnee
	if (CAPTEUR_SAMP_TIME < (millis() - var_stock_temp_capteur))
	{
		// sample time before reading the sensor for a regular interval
		var_stock_temp_capteur = millis();

		lire_Pression_Sensor();
		//test_Trigger_Inspi();
		calcul_Data();
		if (etat != STATE_IDLE)
		{
			// Calcul du correcteur PID ici a chaque fois qu'on lit les capteurs
			servo_angle = calcul_PID(consigne, pression_Patient_Relative, false);
			// calcul angle servo_in et servo out
			servo_in = (int)((float)RATIO_VALVE_INOUT * servo_angle);
			servo_out = (int)(servo_angle);
			sature_Position_Valve_Out(servo_out);			   /////////
			sature_Position_Valve_In(VALVE_IN_MAX - servo_in); ////
		}
	}
// Lecture des Consignes Potentiometre et modification consigne si changement
	if ((POTENTIO_SAMP_TIME < (millis() - var_stock_temp_potentio)) && (stringComplete == false))
	{
		// sample time before reading the sensor for a regular interval
		var_stock_temp_potentio = millis();
		//	lire_Potentiometre();
		//	print_Potentiometre();
	}
// Recuperation consigne de GUI et modification des consignes si besoin
	if (stringComplete)
	{
		//get_PID_From_Uart(); // permet de recuperer les valeurs des coeff PID
		process_Consigne(); //A GARDER POUR LA SUITE ECRAN
	}
// Machine ETAT changement de cycle IDLE INSPI EXPI
	switch (etat)
	{
	case STATE_IDLE:
		// On sort de l'etat de veille si les consignes sont realistes
		//	if ((temps_cycle < 11990) && (consigne_Inspi > 14) && (consigne_Expi > 2))
		//on sort de l'etat de veille si GUI a donne ordre marche
		if (mode_ventilation < 3)
		{
			etat = STATE_INSPIRATION_PLATEAU;
			calcul_PID(0.0, 0.0, true); // useless si ce n'est RAZ PID (notamment le I)
			offset_Capteurs();			// on en profite pour calibrer les capteurs à pression atmo
			delay(100);
			temps_cycle_reel=millis(); //initialisation tps cycle reel
			volume_ml=0;
		}
		break;

	case STATE_INSPIRATION_PLATEAU:
		if ((millis() - var_stock_temp_cycle >= temps_plateauIns)||((is_Regul_Correct==1)&&((debit_l_m<=(debit_max*trigger_expi))&&((mode_ventilation==1)||(mode_ventilation==2)))))
		{
			// sécuité P motrice > 6
			if ((consigne_Inspi - consigne_Expi) < 6.0)
			{
				consigne = consigne_Inspi - 6;
			}
			else
			{
					consigne = consigne_Expi;
			}	
			is_Regul_Correct = 0; // on est plus ok
			etat = STATE_EXPIRATION_PLATEAU;
			var_stock_temp_cycle = millis();
			volume_tidal_ml = 0; // RAZ volume_ml
		}
		break;

	case STATE_EXPIRATION_PLATEAU:
		if (millis() - var_stock_temp_cycle >= temps_plateauExp)
		{
			apnee=1;
			if((mode_ventilation==1)||(mode_ventilation==2))
			{
				//Patient respire mal, on est en respi de sauvegarde
				alarme_to_gui|=1<<2;
			}
			Serial.print("!A");
			Serial.print(alarme_to_gui);
			Serial.println('*');
		}
		if((apnee==1)||(test_Trigger_Expi()&&((mode_ventilation==1)||(mode_ventilation==2))))
		{
			apnee=0; // Reset apnee pour le prochain coup
			consigne = consigne_Inspi;
			is_Regul_Correct = 0; // on est plus ok
			etat = STATE_INSPIRATION_PLATEAU;
			temps_cycle_reel=millis()-var_stock_temp_freq; //calcul tps cycle reel
			var_stock_temp_cycle = millis();
			var_stock_temp_freq=var_stock_temp_cycle;			
			Serial.print("!V");
			Serial.print(volume_tidal_ml);
			Serial.println('*');
			Serial.print("!F");
			Serial.print(temps_cycle_reel);
			Serial.println('*');
			debit_moy+=volume_tidal_ml/1000.0;
			cpt_nb_cycle_min++;
		}
		break;
	}

// Print Value for monitoring
#if PRINT_WHERE == 0
	if (etat != STATE_IDLE)
	{
		if (GUI_SAMP_TIME < (millis() - var_stock_temp_gui))
		{
			// sample time before reading the sensor for a regular interval
			var_stock_temp_gui = millis();
			print_Data_GUI();
		}
	}
	if ((GUI_TELL_STATE < (millis() - var_stock_temp_tell_state)) || (change_pot == true))
	{
		// sample time before reading the sensor for a regular interval
		var_stock_temp_tell_state = millis();
		print_Consigne_Interface_Graphique();
	}
#endif
	/*	if (temps_cycle > 11990)
	{
		etat = STATE_IDLE;
		mode_ventilation=3;
	}	
*/
}

void calcul_Data()
{
	// CALCUL DEBIT ET VOLUME
	
	debit = mesure_Flow_Venturi();								 // en m3/s
	debit_l_m = debit * 60000.0;								 // en litre/min
//	volume_ml += debit * ((float)(now - var_stock_temp_volume));
	if(etat == STATE_EXPIRATION_PLATEAU) // Calcul volume tidal expiration
	{
		now = micros();
		volume_tidal_ml+= debit * ((float)(now - var_stock_temp_volume)); // volume en ml
		var_stock_temp_volume = now;
	} 	
	if(mode_ventilation!=0) // calcul debit max pour Trigger expi ( pas besoin si,mode VPC)
	{
		if(debit_l_m>debit_max)
		{
			debit_max=debit_l_m;
		}
	}
}

char test_Trigger_Expi() // pas utiliser normalement se fait sur Volume pas sur pression
{
	char trig=0;
	if (abs(consigne - pression_Patient_Relative) < DELTA_CONSIGNE)
	{
		is_Regul_Correct = 1; // On a passe les chgt de consigne, variable a remettre a zero des qu'on change un etat
	}
	if (etat == STATE_INSPIRATION_PLATEAU)
	{
		// Detection TRIGGER INSPIRATION
		if (is_Regul_Correct) // remis a 0 achaque cgt d'etat
		{
			if ((pression_Patient_Relative - consigne) > DELTA_CONSIGNE)
			{			  //TRIGGER IS DETECTED
				trig= 5; // trigger detected
			}
			else
			{
				trig= 0;
			}
		}
	}
	return trig;
}

char test_Trigger_Inspi() // detection trigger Inspi si inspi 5 sinon 0...
{
	char trig=0;
	if (abs(consigne - pression_Patient_Relative) < DELTA_CONSIGNE)
	{
		is_Regul_Correct = 1; // On a passe les chgt de consigne, variable a remettre a zero des qu'on change un etat
	}
	// Detection TRIGGER INSPIRATION
	if (is_Regul_Correct)
	{
		if (etat == STATE_EXPIRATION_PLATEAU)
		{
			if (consigne - pression_Patient_Relative > DELTA_CONSIGNE)
			{ //TRIGGER IS DETECTED
				trig = 5;
			}
			else
			{
				trig =0;
			}
		}
	}
	return trig;
}

void print_Data_GUI()
{
	Serial.print("!P");
	Serial.print(pression_Patient_Relative);
	Serial.println('*');
	Serial.print("!D");
	Serial.print(debit_l_m);
	Serial.println('*');
	Serial.print("!c");
	Serial.print(consigne);
	Serial.println('*');
}

void print_Valeurs() // pour lister les donnés sur MONITOR puis export EXCEL
{
	Serial.print(etat); // IDLE INSP EXP
	Serial.print(" (durée ");
	Serial.print(int(pot_A0 / 1000.)); // durée du cycle en sec
	Serial.print("sec @I/cycle ");
	Serial.print(int(pot_A1 * 100)); // ratio Insp/Cycle
	Serial.print("%) (Insp/Exp: ");
	Serial.print(int(pot_A2)); // insp
	Serial.print("/");
	Serial.print(int(pot_A3)); // exp
	Serial.print("cm) [Réel ");
	Serial.print(int(pression_A7)); // pression patient
	Serial.print("cm]");
	Serial.println('*');
}

void lire_Pression_Sensor()
{
	//  read Diff Sensor analog, lecture 3V3 maxi (5V pour range max 100 hPa)
#if TEENSY == 1
	pression_A7 = adc->adc0->analogRead(A7) - pression_offset; // press diff patient, on tiens compte de l'offset du capteur
#else
	pression_A7 = analogRead(A7) - pression_offset;
#endif
	pression_A7 = map(pression_A7, 0, ADC_MAX, 0, MAP_MPXV5004); // Mapping pour Pression MPXV5010
	pression_Patient_Relative = pression_A7 / 100.;
}

void gestion_Alarme()
{
	// Pression mesuree plus petite ou plus grande que capteur => Pb de mesure ou defaut capteur
	if ((pression_Patient_Relative <= MIN_DIFF_PRESS) || (pression_Patient_Relative >= MAX_DIFF_PRESS))
	{
		BUZZER_ON;
		alarme_to_gui|=1<<0; // signifie pb Mesruse
	}
	else if ((consigne_Inspi - consigne_Expi) < 6.0)
	{
		BUZZER_ON;
		alarme_to_gui|=1<<1; // signifie pb consigne potard
	}
	else if (alarme_gui > 0)
	{
		BUZZER_ON;
	}
	else
	{
		BUZZER_OFF;
		alarme_to_gui=0;
	}
	if((alarme_to_gui != alarme_to_gui_old)||(GUI_TELL_STATE < (millis() - var_stock_temp_alarme_gui)))
	{
		Serial.print("!A");
		Serial.print(alarme_to_gui);
		Serial.println('*');
		alarme_to_gui_old=alarme_to_gui;
		var_stock_temp_alarme_gui=millis();
	}
}

void print_Consigne_Interface_Graphique()
{
	Serial.print("!C");
	Serial.print(cycle);
	Serial.println('*');
	Serial.print("!R");
	Serial.print(ratio);
	Serial.println('*');
	Serial.print("!I");
	Serial.print(consigne_Inspi);
	Serial.println('*');
	Serial.print("!E");
	Serial.print(consigne_Expi);
	Serial.println('*');
	Serial.print("!T");
	Serial.print(trigger_expi);
	Serial.println('*');
	Serial.print("!m");
	Serial.print(mode_ventilation);
	Serial.println('*');
}

void get_PID_From_Uart() // NE PAS EFFACER NI MODIFIER !!! on s'en servira plus tard
{
	String temp_String = "";
	char index_Start, index_Stop, index_temp1;
	index_Start = inputString.indexOf('!');
	index_Stop = inputString.indexOf('*');
	if ((index_Start >= 0) && (index_Stop >= 0))
		temp_String = inputString.substring(index_Start, index_Stop);
	if (temp_String != "")
	{
		index_Start = temp_String.indexOf('p'); // coeff P
		index_Stop = temp_String.indexOf('i');	// coeff I
		index_temp1 = temp_String.indexOf('d'); // coeff D
		if (index_Start != -1)
			coeff_P = temp_String.substring(index_Start + 1, index_Stop).toFloat();
		if (index_Stop != -1)
			coeff_I = temp_String.substring(index_Stop + 1, index_temp1).toFloat();
		if (index_temp1 != -1)
			coeff_D = temp_String.substring(index_temp1 + 1).toFloat();
		inputString = "";
	}
}

void process_Consigne() // NE PAS EFFACER NI MODIFIER !!! on s'en servire plus tard
{
	String temp_String = "";
	int index_Start, index_Stop, index_temp1, index_temp2, index_temp3, index_temp4, index_temp5;

	index_Start = inputString.indexOf('!');
	index_Stop = inputString.indexOf('*');
	if ((index_Start >= 0) && (index_Stop >= 0))
		temp_String = inputString.substring(index_Start, index_Stop);
	if (temp_String != "")
	{
		index_Start = temp_String.indexOf('C'); // durée cycle (ms)
		index_Stop = temp_String.indexOf('R');	// ratio Exp/Insp (fraction)
		index_temp1 = temp_String.indexOf('I'); // pression Insp (hPa / cm H2O)
		index_temp2 = temp_String.indexOf('E'); // pression Exp  (hPa / cm H2O)
		index_temp3 = temp_String.indexOf('T'); // Trigger expiratoire en  %
		index_temp4 = temp_String.indexOf('M'); // mode 0 VPC - 1 VNI - 2 VPC-AI
		index_temp5 = temp_String.indexOf('A'); // Alarme sur interface graphique => Buzzer
		if (index_Start != -1)
			cycle = temp_String.substring(index_Start + 1, index_Stop).toFloat();
		if (index_Stop != -1)
			ratio = temp_String.substring(index_Stop + 1, index_temp1).toFloat();
		//On traite deja le cycle et le ratio, car les tps de cycles dependent des 2 parametres
		temps_plateauIns = (unsigned long)(ratio * cycle / 100.0);
		temps_plateauExp = (unsigned long)cycle - temps_plateauIns;
		temps_cycle = temps_plateauIns + temps_plateauExp;
		if (index_temp1 != -1)
			consigne_Inspi = temp_String.substring(index_temp1 + 1, index_temp2).toFloat();
		if (index_temp2 != -1)
			consigne_Expi = temp_String.substring(index_temp2 + 1, index_temp3).toFloat();
		if (index_temp3 != -1)
			trigger_expi = temp_String.substring(index_temp3 + 1, index_temp4).toFloat();
		if (index_temp4 != -1)
			mode_ventilation = (unsigned char)temp_String.substring(index_temp4 + 1, index_temp5).toInt();
		if (index_temp5 != -1)
			alarme_gui = (unsigned char)temp_String.substring(index_temp5 + 1).toInt();
		inputString = "";
		print_Consigne_Interface_Graphique();
	}
	if (mode_ventilation >= 3)
	{
		etat = STATE_IDLE;
	}
	stringComplete = false;
}

float calcul_PID(float consigne, float sensor, bool init_val)
{
	// Calcul temps de boucle ( devrait etre constant en toute rigueur )
	unsigned long now = micros();
	unsigned long dt = now - last_time;
	float delta_t = (float)dt;
	delta_t = delta_t / 1000000.0; // microseconds => sec
	if (init_val == true)
	{
		err_int = 0;
		last_erreur = 0;
	}
	//Calcul des erreurs
	float erreur = consigne - sensor;
	err_int += (erreur * delta_t);
	float err_der = (erreur - last_erreur) / delta_t;

	//Calcul sortie PID
	float Output = coeff_P * erreur + coeff_I * err_int + coeff_D * err_der;
	// Memoriser variable pour le prochain pas
	last_erreur = erreur;
	last_time = now;
	return Output;
}

void sature_Position_Valve_In(int position)
{
	// Saturation commande sortie PID
	// on veut saturer la sortie entre 0 et VALVE_OUT_MAX,
	// si position > MAX alors VALVE = VALVE_OUT_MAX, si position <0 alors VALVE=0
	// sinon si position est comprise entre les deux VALVE = position

	if (position > VALVE_IN_MAX)
	{
		inValve.write(VALVE_IN_MAX);  //   |
	}								  //   |
	else if (position < VALVE_IN_MIN) //   |
	{								  //   |
		inValve.write(VALVE_IN_MIN);  //    >  si erreur augmente on ferme la VALVE...
	}								  //   |    donc un - devant servo angle
	else							  //   |
	{								  //   |
		inValve.write(position);	  //   |
	}
}

void sature_Position_Valve_Out(int position)
{
	// Saturation commande sortie PID
	// on veut saturer la sortie entre 0 et VALVE_OUT_MAX,
	// si position > MAX alors VALVE = VALVE_OUT_MAX, si position <0 alors VALVE=0
	// sinon si position est comprise entre les deux VALVE = position
	if (position > VALVE_OUT_MAX)
	{
		outValve.write(VALVE_OUT_MAX); //   |
	}								   //   |
	else if (position < VALVE_OUT_MIN) //   |
	{								   //   |
		outValve.write(VALVE_OUT_MIN); //    >  si erreur augmente on ferme la VALVE...
	}								   //   |    donc un - devant servo angle
	else							   //   |
	{								   //   |
		outValve.write(position);	   //   |
	}
}

float mesure_Flow_Venturi()
{
#define VENTURI_S0 50.26e-6 // section venturi en entree en m2 phi 8
#define VENTURI_S1 12.56e-6 // section venturi au col en m2 phi 4
#define RHO 1.2				// densité air kg/m3
#define S0_CARRE 2.526e-9	// S0² uniquement utile pour alleger tps de calcul Teensy
#define S1_CARRE 157.75e-12 // S1²
#define DEUX_S0_CARRE 5.0526e-9
#define RHO_SURF_DIFF 2.842e-9 // Rho*(S0*S0 -S1*S1)
	float debit_out,debit_in,debit;			   // contient debit en m3/s

#if TEENSY == 1
	venturi_out_A6 = adc->adc0->analogRead(A6) - venturi_out_offset; // on lit la patte A6 = venturi et on retranche l'offset
	venturi_out_A8 = adc->adc0->analogRead(A8) - venturi_in_offset; // on lit la patte A8 = venturi et on retranche l'offset
#else
	venturi_out_A6 = analogRead(A6) - venturi_out_offset;
	venturi_in_A8 = analogRead(A8) - venturi_in_offset;
#endif
	// venturi_A6 doit etre en Pa (et non hPa) pour les calculs suivants:
	// on calibre capteur MPXV5010
	venturi_out_A6 = map(venturi_out_A6, 0, ADC_MAX, 0, MAP_MPXV5010);
	venturi_in_A8 = map(venturi_in_A8, 0, ADC_MAX, 0, MAP_MPXV5010);
	if (venturi_out_A6 >= 0)
	{
		debit_out = VENTURI_S1 * sqrt((DEUX_S0_CARRE * venturi_out_A6) / (RHO_SURF_DIFF));
	}
	else
	{
		debit_out = 0;
	}
	if (venturi_in_A8 >= 0)
	{
		debit_in = VENTURI_S1 * sqrt((DEUX_S0_CARRE * venturi_in_A8) / (RHO_SURF_DIFF));
	}
	else
	{
		debit_in = 0;
	}
	debit=debit_in-debit_out;
	return debit;
}

void offset_Capteurs()
{
	unsigned char i;

	// valves IN fermée, valve OUT ouverte, on élimine la pression dans le circuit
	outValve.write(VALVE_OUT_MIN); // valve OUT OPEN
	inValve.write(VALVE_IN_MAX);   // valve IN CLOSED
	delay(2000);				   // on laisse tomber la pression
	for (i = 0; i < 64; i++)	   // moyennage sur 64 Valeur
	{
#if TEENSY == 1
		venturi_out_offset += adc->adc0->analogRead(A6);
		venturi_in_offset += adc->adc0->analogRead(A8);
		pression_out_offset += adc->adc0->analogRead(A7);
#else
		venturi_out_offset += analogRead(A6);
		venturi_in_offset += analogRead(A8);
		pression_offset += analogRead(A7);
#endif
		delay(10);
	}
	venturi_out_offset = venturi_out_offset / 64;
	venturi_in_offset = venturi_in_offset / 64;
	pression_offset = pression_offset / 64;
#if PRINT_WHERE != 0
	Serial.print("Offset(V_OUT");
	Serial.print(venturi_out_offset);
	Serial.print("/P_");
	Serial.print(pression_offset);
	Serial.print(")");
#endif
}

void lire_Potentiometre()
{
#if TEENSY == 1
	adc->adc0->setAveraging(1);												   // set number of averages	to 1 for lecture potards
	pot_A0 = map(adc->adc0->analogRead(A0), 0, ADC_MAX, 12000, 1000);		   // cycle 1000 à 12000 millisec (en tournant à droite 5 à 60 cycles/min)
	pot_A1 = map(adc->adc0->analogRead(A1), 0, ADC_MAX, 2000, 5000) / 100.0; // ratio Insp/Cycle 0,2 à 0,5 : voir NB ci-dessous, y'a une nuance !
	pot_A2 = map(adc->adc0->analogRead(A2), 0, ADC_MAX, 1500, 3500) / 100.0;   // inspi 15 à 30 cmH2O (30 au lieu de 40 tant qu'on n'a pas la bonne turbine)
	pot_A3 = map(adc->adc0->analogRead(A3), 0, ADC_MAX, 300, 1600) / 100.0;	   // expi 3 à 16 cmH2O
	adc->adc0->setAveraging(16);											   // set number of averages to 16 pour capteurs pression
#else
	pot_A0 = map(analogRead(A0), 0, ADC_MAX, 12000, 1000);		  // cycle 1000 à 12000 millisec (en tournant à droite 5 à 60 cycles/min)
	pot_A1 = map(analogRead(A1), 0, ADC_MAX, 2000, 5000) / 100.0; // ratio Insp/Cycle 0,2 à 0,5 : voir NB ci-dessous, y'a une nuance !
	pot_A2 = map(analogRead(A2), 0, ADC_MAX, 1500, 3500) / 100.0; // inspi 15 à 30 cmH2O (30 au lieu de 40 tant qu'on n'a pas la bonne turbine)
	pot_A3 = map(analogRead(A3), 0, ADC_MAX, 300, 1600) / 100.0;  // expi 3 à 16 cmH2O
#endif
	// ATTENTION le ratio affiché c'est le ratio insp/cycle et non pas I/E (Insp/Exp)
	// les réa pensent en ratio I/E de 1/4 à 1/1
	// MAIS comme Cycle = Insp + Exp, alors Insp/Cycle = 20 à 50%
	// DONC le potard donne direct 20 à 50% du cycle puisque le calculateur pense en insp/cycle
	// il faudra PLUS TARD demander au potard de donner et afficher le vrai I/E et non I/cycle, et de rajouter une ligne de calcul
	// PAS BESOIN REGLE SUR L4INTERFACE GRAPHIQUE
}

void print_Potentiometre()
{
	change_pot = false;
	if (abs(pot_A0 - pot_A0_old) > 100) // vario cycle 100 ms
	{
		pot_A0_old = pot_A0;
		cycle = pot_A0;
#if PRINT_WHERE != 0
		Serial.print("(durée=");
		Serial.print(int(pot_A0 / 100.) / 10.); // durée du cycle en sec
		Serial.print(")");
#endif
		temps_cycle = (unsigned long)cycle;
		temps_plateauIns = (unsigned long)(cycle * ratio / 100.0); // NB I/E n'est pas I/cycle ! I/E = ratio/(1-ratio) on en tient compte sur l'interface graphique
		temps_plateauExp = temps_cycle - temps_plateauIns;
		change_pot = true;
	}
	if (abs(pot_A1 - pot_A1_old) > 0.02) // vario ratio 2%
	{
		pot_A1_old = pot_A1;
		ratio = pot_A1;
		temps_plateauIns = (unsigned long)(cycle * ratio / 100.0);
		temps_plateauExp = temps_cycle - temps_plateauIns;
		change_pot = true;
	}
	if (abs(pot_A2 - pot_A2_old) > 0.2) // vario Inspi 0.2 cm
	{
		pot_A2_old = pot_A2;
		consigne_Inspi = pot_A2;
		change_pot = true;
	}
	if (abs(pot_A3 - pot_A3_old) > 0.2) // vario expi 0,2 cm
	{
		pot_A3_old = pot_A3;
		consigne_Expi = pot_A3;
		change_pot = true;
	}
#if PRINT_WHERE == 1
	plot_Valeurs(); //  TRACEUR
#elif PRINT_WHERE == 2
	print_Valeurs();											  //  TABLEAU
#else
	if (change_pot == true)
		print_Consigne_Interface_Graphique();
#endif
}

void plot_Valeurs() // pour PLOTTER les courbes
{
	// CONSIGNES:
	Serial.print(pot_A2);
	Serial.print(','); // P insp
	Serial.print(pot_A3);
	Serial.print(','); // PEP
	Serial.print(consigne);
	Serial.print(','); // consigne qui basule de A2 (insp) à A3 (PEP)
	// ANGLES:
	//     Serial.print(servo_in/10.); Serial.print(',');    // angle du servo_in /10
	//     Serial.print(servo_out/10.); Serial.print(',');   // angle du servo_out /10
	// DEBIT & VOLUME:
	//     Serial.print(venturi_our_A6/100.); Serial.print(','); // pression venturi en Pa/100. = cmH2O ou hPa
	Serial.print(debit_l_m);
	Serial.print(','); // debit en litre/min
	Serial.print(volume_ml / 10.);
	Serial.print(','); // volume en ml/10. = cl (car en ml trop grand pour écran)
	// PRESSION:
	Serial.print(pression_Patient_Relative);
	Serial.print(','); // pression Patient cmH2O ou hPa
	//     Serial.print(consigne-pression_Patient_Relative);  Serial.print(','); // en bas écart à la consigne
	//     Serial.print(is_Regul_Correct); Serial.print(',');  // détect
	//     Serial.print(trigger); // en bas seuil de delta

	Serial.println(','); // FIN de plot
	trigger = 0;
}

/*
SerialEvent occurs whenever a new data comes in the hardware serial RX. 
This routine is run between each time loop() runs, so using delay inside loop 
can delay response. Multiple bytes of data may be available.
*/
void serialEvent()
{ // pour LIRE commandes AU CLAVIER
	while (Serial.available())
	{
		// get the new byte:
		char inChar = (char)Serial.read();
		// add it to the inputString:
		inputString += inChar;
		// if the incoming character is an asterix ;-) ,
		// set a flag so the main loop can do something about it:
		if (inChar == '*')
		{
			stringComplete = true;
		}
	}
}