 
#pragma region COMENTARIOS

/*

Controlador ASCOM para BMDome1
Description: Driver para controlador de Domo desarrollado por Bilbaomakers
			 para el proyecto de observatorio astronomico en Marcilla de Campos.
			 Control de azimut de la cupula y apertura cierre del shutter.
			 Hardware de control con Arduino y ESP8266
			 Comunicaciones mediante MQTT
Implements:	 ASCOM Dome interface version: 6.4SP1
Author:		 Diego Maroto - BilbaoMakers 2019 - info@bilbaomakers.org



Cosas a hacer en el futuro

- Implementar uno a varios Led WS2812 para informar del estado de lo importante
- Implementar calculo de rotacion basado en mi posicion para "predecir" el movimiento del objeto y poder hacer seguimiento auntonomo automatico
				
Cosas de configuracion a pasar desde el Driver

		- El Azimut cuando esta en HOME
		- El Azimut del PARK


NOTAS SOBRE EL STEPPER Y LA LIBRERIA ACCELSTEPPER

- El ancho minimo de pulso que puede generar la libreria es 20uS, por tanto el periodo minimo es 40uS - Frecuencia Maxima 25Khz
- Una velocidad de motor aceptable inicialmente son 50 revoluciones por minuto (50 tiene el motor de puerta corredera actual pero como soy un maniatico me gusta mas 60)
- Eso es 1 vuelta por segundo en la reductora, que como es 1:6 son 6 revoluciones / segundo del motor
- A 3200 pulsos por vuelta en la controladora, para 6 rev/segundo son 19200 pulsos por segundo. Es el maximo que puedo exprimir la libreria.
- Determinaremos la velocidad optima de la cupula "in situ", originalmente probaremos mas lento cuando montemos.

- Para ejecutar el comando "run" de la libreria Accelstepper vamos a usar la interrupcion de un timer.
- ESP32 tiene 4 timers que funcionan a una frecuencia de 80 MHz (Ole!). Nos sobra "velocidad" que te cagas aqui
- No tengo ni idea de cuantos ciclos de reloj usa el run() de la libreria (lo investigare a ver)

################################################################################

Funcionalidades del cuadro de usuario.

LUZ ROJA - Avisos
	ERROR - 3 pulsos largos (seta apretada al armar, mover sin armar, .....)
	Aviso AbortSlew - 2 pulsos medios
	Aviso buscando casa - 1 pulso largo
	Aviso al terminar de iniciar el sistema - 3 pulsos cortos rapidos
	Aviso botones - Click 1 pulso corto - Hold 2 pulsos cortos

LUZ VERDE - Comunicaciones
	Conectando a la wifi - 1 pulso por segundo
	Conectando al MQTT - 2 pulsos cortos y 1 segundo idle
	Comunicaciones OK - Permanente

LUZ AZUL - ESTADO DEL 
	HW NOT INIT - Apaga
	HW OK READY - Encendido
	SLEWING - 1 pulso por segundo

BOTON1 - AZUL
	CLICK - Inicializa Hardware STD (busca home)
	HOLD - Inicializa Hardware FORCE (sin buscar home)
	
BOTON2 - VERDE
	CLICK - Mover a 90 (SlewToAZimut)
	HOLD - Set Park en la posicion actual (SetParkHere)
	
BOTON3 - ROJO
	CLICK - Parada Normal (AbortSlew)
	HOLD - Aparcar (Park)

BOTON4 - VERDE
	CLICK - Mover a 270 (SlewToAZimut)
	HOLD - 

*/

#pragma endregion

#pragma region INCLUDES
// Librerias comantadas en proceso de sustitucion por la WiFiMQTTManager

#include <AsyncMqttClient.h>			// Vamos a probar esta que es Asincrona: https://github.com/marvinroger/async-mqtt-client
#include <AccelStepper.h>				// Para controlar el stepper como se merece: https://www.airspayce.com/mikem/arduino/AccelStepper/classAccelStepper.html
#include <FS.h>							// Libreria Sistema de Ficheros
#include <WiFi.h>						// Para las comunicaciones WIFI del ESP32
#include <DNSServer.h>					// La necesita WifiManager para el portal captivo
#include <WebServer.h>					// La necesita WifiManager para el formulario de configuracion (ESP32)
#include <ArduinoJson.h>				// OJO: Tener instalada una version NO BETA (a dia de hoy la estable es la 5.13.4). Alguna pata han metido en la 6
#include <string>						// Para el manejo de cadenas
#include <Bounce2.h>					// Libreria para filtrar rebotes de los Switches: https://github.com/thomasfredericks/Bounce2
#include <SPIFFS.h>						// Libreria para sistema de ficheros SPIFFS
#include <NTPClient.h>					// Para la gestion de la hora por NTP
#include <WiFiUdp.h>					// Para la conexion UDP con los servidores de hora.
#include <OneButton.h> 					// Para los botones de usuario: https://github.com/mathertel/OneButton
#include <IndicadorLed.h>				// Mi libreria para Leds

#pragma endregion

#pragma region Constantes y configuracion. Modificable aqui por el usuario

#define CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU1 0

// Para la configuracion de conjunto Mecanico de arrastre
static const uint8_t MECANICA_STEPPER_PULSEPIN = 25;					// Pin de pulsos del stepper
static const uint8_t MECANICA_STEPPER_DIRPIN = 33;						// Pin de direccion del stepper
static const uint8_t MECANICA_STEPPER_ENABLEPING = 32;				// Pin de enable del stepper
static const short MECANICA_PASOS_POR_VUELTA_MOTOR = 400;		// Numero de pasos por vuelta del STEPPER (Configuracion del controlador)
static const float MECANICA_STEPPER_MAXSPEED = (MECANICA_PASOS_POR_VUELTA_MOTOR * 5);	// Velocidad maxima del stepper (pasos por segundo)
static const float MECANICA_STEPPER_MAXSPEED_INST = (MECANICA_PASOS_POR_VUELTA_MOTOR * 2);	// Velocidad maxima del stepper en modo instalador (pasos por segundo)
static const float MECANICA_STEPPER_MAXACELERATION = (MECANICA_STEPPER_MAXSPEED / 3);	// Aceleracion maxima del stepper (pasos por segundo2). Aceleraremos al VMAX en 3 vueltas del motor.
static const float MECANICA_STEPPER_MAXACELERATION_INST = (MECANICA_STEPPER_MAXSPEED_INST / 3);	// Aceleracion maxima del stepper modo instalador(pasos por segundo2). Aceleraremos al VMAX en 3 vueltas del motor.
static const short MECANICA_RATIO_REDUCTORA = 6;							// Ratio de reduccion de la reductora
static const short MECANICA_DIENTES_PINON_ATAQUE = 16;				// Numero de dientes del piños de ataque
static const short MECANICA_DIENTES_CREMALLERA_CUPULA = 981;	// Numero de dientes de la cremallera de la cupula
static const boolean MECANICA_STEPPER_INVERTPINS = false;			// Invertir la logica de los pines de control (pulso 1 o pulso 0)
static const int MECANICA_STEPPER_ANCHO_PULSO = 10;						// Ancho de los pulsos

// Entradas
static const uint8_t MECANICA_BOTONPANEL1 = 15;
static const uint8_t MECANICA_BOTONPANEL2 = 13;
static const uint8_t MECANICA_BOTONPANEL3 = 12;
static const uint8_t MECANICA_BOTONPANEL4 = 14;
static const uint8_t MECANICA_EMERGENCY_STOP = 27;
static const uint8_t MECANICA_SENSOR_HOME = 26;								// Pin para el sensor de HOME

// Salidas
static const uint8_t MECANICA_LEDROJO = 16;
static const uint8_t MECANICA_LEDVERDE = 17;
static const uint8_t MECANICA_LEDAZUL = 5;
static const uint8_t MECANICA_SALIDA4 = 19;
static const uint8_t MECANICA_SALIDA5 = 21;
static const uint8_t MECANICA_SALIDA6 = 22;
static const uint8_t MECANICA_SALIDA7 = 23;

// Para el ticker del BMDomo1
unsigned long TIEMPO_TICKER_RAPIDO = 500;

// Para la zona horaria (horas de diferencia con UTC)
static const int HORA_LOCAL = 2;

// PARA MODO INSTALADOR. Para que la cupula tenga la funcionalidad reducida lo justo para el instalador
static const boolean MODO_INSTALADOR = true;


#pragma endregion

#pragma region DEFINICIONES

enum TipoEstadoMecanica{

	MEC_NOT_INIT,
	MEC_INITIALIZING,
	MEC_OK,
	MEC_EMERGENCY,

}estadoMecanica;

enum TipoEstadoMovimiento {

	MOV_BUSCANDOCASA,
	MOV_APARCANDO,
	MOV_OKSTOPPED,
	MOV_OKSLEWING,
	MOV_ERROR,

}estadoMovimiento;

enum TipoEstadoComunicaciones {

	COM_OK,

}estadoComunicaciones;


// VARIABLES
//bool Inicializando;							// Para saber que estamos ejecutando el comando INITHW
//bool BuscandoCasa;							// Para saber que estamos ejecutando el comando FINDHOME
//bool Aparcando;								// Para saber si estamos yendo a aparcar
float TotalPasos;							// Variable para almacenar al numero de pasos totales para 360º (0) al iniciar el objeto.
bool HayQueSalvar;							// 
String HardwareInfo;						// Identificador del HardWare y Software
bool ComOK;									// Si la wifi y la conexion MQTT esta OK
bool DriverOK;								// Si estamos conectados al driver del PC y esta OK
//bool HardwareOK;							// Si nosotros estamos listos para todo (para informar al driver del PC)
//bool Slewing;								// Si alguna parte del Domo esta moviendose
bool AtHome;								// Si esta parada en HOME
bool AtPark;								// Si esta en la posicion de PARK									
int ParkPos;								// Para almacenar la posicion de Park en la clase
//bool emergencyStop;							// Para almacenar el estado de la seta de emergencia
// Para el sensor de temperatura de la CPU. Definir aqui asi necesario es por no estar en core Arduino.
extern "C" {uint8_t temprature_sens_read();}

// FUNCIONES
String MiEstadoJson(int categoria);			// Devuelve un JSON con los estados en un array de 100 chars (la libreria MQTT no puede con mas de 100)
void MoveTo(int azimut);					// Mover la cupula a un azimut
void IniciaCupula(String parametros);		// Inicializar la cupula
void ApagaCupula(String parametros);		// Apagar la cupula normalmente.
void FindHome();							// Mueve la cupula a Home
long GetCurrentAzimut();					// Devuelve el azimut actual de la cupula
void Run();									// Actualiza las propiedades de estado de este objeto en funcion del estado de motores y sensores
void Connected();							// Para implementar la propiedad-metodo Connected
void Connected(boolean status);				// Para implementar la propiedad-metodo Connected
void AbortSlew();
void SetPark(int l_ParkPos);
void Park();
boolean LeeConfig();
void MandaRespuesta(String comando, String payload);		
long GradosToPasos(long grados);			// Convierte Grados a Pasos segun la mecanica
long PasosToGrados(long pasos);		  		// Convierte pasos a grados segun la mecanica
boolean SalvaConfig();
void EmergencyStop();						// Parada de emergencia de la cupula

// OBJETOS
Bounce Debouncer_HomeSwitch = Bounce(); 	// Objeto debouncer para el switch HOME
Bounce Debouncer_Emergency_Stop = Bounce(); 	// Objeto debouncer para la seta de emergencia

// Para la conexion MQTT
AsyncMqttClient  clienteMqtt;

// Controlador Stepper
AccelStepper controladorStepper;

// Los manejadores para las tareas. El resto de las cosas que hace nuestro controlador que son un poco mas flexibles que la de los pulsos del Stepper
TaskHandle_t tHandleTaskCupulaRun,tHandleTaskProcesaComandos,tHandleTaskComandosSerieRun,tHandleTaskMandaTelemetria,tHandleTaskGestionRed,tHandleTaskEnviaRespuestas, tHandleTaskGestionCuadro;	

// Manejadores Colas para comunicaciones inter-tareas
QueueHandle_t colaComandos,colaRespuestas;

// Timer Stepper Run
hw_timer_t * timer_stp = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// Flag para el estado del sistema de ficheros
boolean spiffStatus = false;

// Conexion UDP para la hora
WiFiUDP udpNtp;

// Manejador del NTP. Cliente red, servidor, offset zona horaria, intervalo de actualizacion.
// FALTA IMPLEMENTAR ALGO PARA CONFIGURAR LA ZONA HORARIA
NTPClient clienteNTP(udpNtp, "europe.pool.ntp.org", HORA_LOCAL * 3600, 3600);

// Cuado Mando
IndicadorLed ledRojo (MECANICA_LEDROJO, false);
IndicadorLed ledVerde (MECANICA_LEDVERDE, false);
IndicadorLed ledAzul (MECANICA_LEDAZUL, false);
OneButton boton1 (MECANICA_BOTONPANEL1,true,true);
OneButton boton2 (MECANICA_BOTONPANEL2,true,true);
OneButton boton3 (MECANICA_BOTONPANEL3,true,true);
OneButton boton4 (MECANICA_BOTONPANEL4,true,true);

#pragma endregion

#pragma region CLASE ConfigClass

class ConfigClass{

	private:

		String c_fichero;
	
	public:
	
		char mqttserver[40];
		char mqttport[6];
		char mqtttopic[33];
		char mqttusuario[19];
		char mqttpassword[25];

		String cmndTopic;
		String statTopic;
		String teleTopic;
		String lwtTopic;

		// Esto no se salva en el fichero, lo hace el objeto Wifi
		// Lo pongo aqui como almacenamiento temporal para los comandos de configuracion
		char Wssid[30];
		char WPasswd[100];


		// Otras configuraciones permanentes del la cupula
		int ParkPos;

		ConfigClass(String fichero);
		~ConfigClass() {};

		boolean leeconfig ();
		boolean escribeconfig ();
		
};

	// Constructor
	ConfigClass::ConfigClass(String fichero){

		c_fichero = fichero;

		//leeconfig();

		mqttserver[0]= '\0';
		mqttport[0] = '\0';
		mqtttopic[0] = '\0';
		mqttusuario[0] = '\0';
		mqttpassword[0] = '\0';

		Wssid[0] = '\0';
		WPasswd[0]  = '\0';	
				
	}

	// Seer la configuracion desde el fichero
	boolean ConfigClass::leeconfig(){
	
		if (SPIFFS.exists(c_fichero)) {
			// Si existe el fichero abrir y leer la configuracion y asignarsela a las variables definidas arriba
			File ComConfigFile = SPIFFS.open(c_fichero, "r");
			if (ComConfigFile) {
				size_t size = ComConfigFile.size();
				// Declarar un buffer para almacenar el contenido del fichero
				std::unique_ptr<char[]> buf(new char[size]);
				// Leer el fichero al buffer
				ComConfigFile.readBytes(buf.get(), size);
				DynamicJsonBuffer jsonBuffer;
				JsonObject& json = jsonBuffer.parseObject(buf.get());
				//json.printTo(Serial);
				
				if (json.success()) {
					Serial.print("Configuracion del fichero leida: ");
					json.printTo(Serial);
				    Serial.println("");

					// Leer los valores del MQTT
					strcpy(mqttserver, json["mqttserver"]);
					strcpy(mqttport, json["mqttport"]);
					strcpy(mqtttopic, json["mqtttopic"]);
					strcpy(mqttusuario, json["mqttusuario"]);
					strcpy(mqttpassword, json["mqttpassword"]);

					// Dar valor a las strings con los nombres de la estructura de los topics
					cmndTopic = "cmnd/" + String(mqtttopic) + "/#";
					statTopic = "stat/" + String(mqtttopic);
					teleTopic = "tele/" + String(mqtttopic);
					lwtTopic = teleTopic + "/LWT";
					return true;

					Serial.println("Servidor MQTT: " + String(mqttserver)) + ":" + String(mqttport);
					Serial.println("Topic Comandos: " + cmndTopic);
					Serial.println("Topic Respuestas: " + statTopic);
					Serial.println("Topic Telemetria: " + teleTopic);
					Serial.println("Topic LWT: " + lwtTopic);
										
				}
					
				else {

					Serial.println("No se puede cargar la configuracion desde el fichero");
					return false;

				}
			}

			else {

				Serial.println ("No se puede leer el fichero de configuracion");
				return false;

			}

		}

		else	{

				Serial.println("No se ha encontrado un fichero de configuracion.");
				Serial.println("Por favor configura el dispositivo desde el terminal serie y reinicia el controlador.");
				return false;

		}

	}
	
	// Salvar la configuracion en el fichero
	boolean ConfigClass::escribeconfig(){

		Serial.println("Salvando la configuracion en el fichero");
		DynamicJsonBuffer jsonBuffer;
		JsonObject& json = jsonBuffer.createObject();
		json["mqttserver"] = mqttserver;
		json["mqttport"] = mqttport;
		json["mqtttopic"] = mqtttopic;
		json["mqttusuario"] = mqttusuario;
		json["mqttpassword"] = mqttpassword;
		
		File ComConfigFile = SPIFFS.open(c_fichero, "w");
		if (!ComConfigFile) {
			Serial.println("No se puede abrir el fichero de configuracion de las comunicaciones");
			return false;
		}

		//json.prettyPrintTo(Serial);
		json.printTo(ComConfigFile);
		ComConfigFile.close();
		Serial.println("Configuracion Salvada");
		
		// Dar valor a las strings con los nombres de la estructura de los topics
		cmndTopic = "cmnd/" + String(mqtttopic) + "/#";
		statTopic = "stat/" + String(mqtttopic);
		teleTopic = "tele/" + String(mqtttopic);
		lwtTopic = teleTopic + "/LWT";

		return true;

	}

	// Objeto de la clase ConfigClass (configuracion guardada en el fichero)
	ConfigClass MiConfig = ConfigClass("/Comunicaciones.json");

#pragma endregion

#pragma region Funciones Cupula

// Para Inicializar variables y demas.
void InitObjCupula() {	

	HardwareInfo = "BMDome1.HWAz.1.0";
	//Inicializando = false;
	ComOK = false;
	DriverOK = false;
	//HardwareOK = false;
	//Slewing = false;			
	//BuscandoCasa = false;
	AtHome = false;
	AtPark = false;
	//Aparcando = false;

	estadoMecanica=MEC_NOT_INIT;
	estadoMovimiento=MOV_ERROR;

	TotalPasos = (float)(MECANICA_DIENTES_CREMALLERA_CUPULA * MECANICA_RATIO_REDUCTORA * MECANICA_PASOS_POR_VUELTA_MOTOR) / (float)(MECANICA_DIENTES_PINON_ATAQUE);
	// Inicializacion del sensor de HOME
	pinMode(MECANICA_SENSOR_HOME, INPUT_PULLUP);
	Debouncer_HomeSwitch.attach(MECANICA_SENSOR_HOME);
	Debouncer_HomeSwitch.interval(30);
	// Inicializacion del sensor Seta Emergencia
	pinMode(MECANICA_EMERGENCY_STOP, INPUT_PULLUP);
	Debouncer_Emergency_Stop.attach(MECANICA_EMERGENCY_STOP);
	Debouncer_Emergency_Stop.interval(5);

	ParkPos = 0;
	HayQueSalvar = false;

}

// Traduce Grados a Pasos segun la mecanica
long GradosToPasos(long grados) {

	return round((float)(grados * TotalPasos) / 360);
	
}

// Traduce Pasos a Grados segun la mecanica
long PasosToGrados(long pasos) {

	// Con una pequeña correccion porque a veces si se pide la posicion por encima de 359.5 devuelve 360 (por el redondeo) y no vale, tiene que ser 0
	long t_grados = round((float)(pasos * 360) / (float)TotalPasos);
	
	if (t_grados == 360) {

		return 0;

	}
		
	else {

		return t_grados;

	}
}

// Metodo que devuelve un JSON con el estado
String MiEstadoJson(int categoria) {

	DynamicJsonBuffer jBuffer;
	JsonObject& jObj = jBuffer.createObject();

	// Dependiendo del numero de categoria en la llamada devolver unas cosas u otras
	switch (categoria)
	{

	case 1:

		// Esto llena de objetos de tipo "pareja propiedad valor"
		jObj.set("TIME", clienteNTP.getFormattedTime());							// HORA
		jObj.set("HI", HardwareInfo);												// Info del Hardware
		jObj.set("CS", ComOK);														// Info de la conexion WIFI y MQTT
		jObj.set("DS", DriverOK);													// Info de la comunicacion con el DRIVER ASCOM (o quiza la cambiemos para comunicacion con "cualquier" driver, incluido uno nuestro
		jObj.set("HS", HardwareOK);													// Info del estado de inicializacion de la mecanica
		jObj.set("AZ", GetCurrentAzimut());											// Posicion Actual (AZ)
		jObj.set("ATH", AtHome); 													// Propiedad AtHome
		jObj.set("PRK", ParkPos);													// Posicion almacenada de Park
		jObj.set("ATPRK", AtPark);													// Propiedad AtPark
		jObj.set("POS", controladorStepper.currentPosition());  					// Posicion en pasos del objeto del Stepper
		jObj.set("TOT", TotalPasos);												// Numero total de pasos por giro de la cupula
		jObj.set("MAXT", controladorStepper.maxexectime());							// Tiempo de ejecucion maxima del ciclo Stepper
		jObj.set("RSSI", WiFi.RSSI());												// RSSI de la señal Wifi
		jObj.set("HALL", hallRead());												// Campo magnetico en el MicroProcesador
		jObj.set("ITEMP", (int)(temprature_sens_read() - 32) / 1.8);				// Temperatura de la CPU convertida a Celsius.

		break;

	case 2:

		jObj.set("INFO2", "INFO2");							
		
		break;

	default:

		jObj.set("NOINFO", "NOINFO");						// MAL LLAMADO

		break;
	}


	// Crear un buffer (aray de 100 char) donde almacenar la cadena de texto del JSON
	char JSONmessageBuffer[200];

	// Tirar al buffer la cadena de objetos serializada en JSON con la propiedad printTo del objeto de arriba
	jObj.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));

	// devolver el char array con el JSON
	return JSONmessageBuffer;
	
}

// Metodos (funciones). TODAS Salvo la RUN() deben ser ASINCRONAS. Jamas se pueden quedar uno esperando. Esperar a lo bobo ESTA PROHIBIDISISISISMO, tenemos MUCHAS cosas que hacer ....
void IniciaCupula(String parametros) {

	switch (estadoMecanica)
	{
	case MEC_EMERGENCY:
		ledRojo.Pulsos(1500,500,3);
		MandaRespuesta("InitHW", "ERR_EMERG");
		break;

	case MEC_OK:
		MandaRespuesta("InitHW", "READY");
		break;

	case MEC_NOT_INIT:

		if (parametros == "FORCE") {

			estadoMecanica=MEC_OK;
			MandaRespuesta("InitHW", "READY");
			controladorStepper.enableOutputs();
			
		}

		else if (parametros == "STD"){


			//Inicializando = true;
			estadoMecanica=MEC_INITIALIZING;

			// Activar el motor. el resto del objeto Stepper ya esta OK
			controladorStepper.enableOutputs();

			// Como el metodo ahora mismo es dar toda la vuelta, hago cero. Si cambio el metodo esto no valdria.
			controladorStepper.setCurrentPosition(0);

			// Busca Home (Asyncrono)
			FindHome();

		}
		break;

	default:
		break;
	}
	
}

// Funcion que da orden de mover la cupula para esperar HOME.
void FindHome() {


	switch (estadoMecanica){

	case MEC_NOT_INIT:
		MandaRespuesta("FindHome", "ERR_HNI");
		ledRojo.Pulsos(1500,500,3);
		break;
		
	case MEC_EMERGENCY:
		MandaRespuesta("FindHome", "ERR_EME");
		ledRojo.Pulsos(1500,500,3);
		break;

	case MEC_INITIALIZING: case MEC_OK:

			switch (estadoMovimiento)
			{
				case MOV_OKSLEWING:
					MandaRespuesta("FindHome", "ERR_SLW");
					ledRojo.Pulsos(1500,500,3);
					break;
				
				case MOV_BUSCANDOCASA:
					MandaRespuesta("FindHome", "ERR_FH");
					ledRojo.Pulsos(1500,500,3);
					break;

				case MOV_APARCANDO:
					MandaRespuesta("FindHome", "ERR_PRK");
					ledRojo.Pulsos(1500,500,3);
					break;

				case MOV_OKSTOPPED:

					if (AtHome) {

						// Caramba ya estamos en Home y Parados No tenemos que hacer nada
						MandaRespuesta("FindHome", "ATHOME");
						break;

					}

					else{

						MandaRespuesta("FindHome", "CMD_OK");

						// Pues aqui si que hay que movernos hasta que encontremos casa y pararnos.
						// Aqui nos movemos (de momento a lo burro a dar media vuelta entera)
						MoveTo(180);

						// Pero tenemos que salir de la funcion esta no podemos estar esperando a que de la vuelta asi que activo el flag para saber que "estoy buscando home" y termino
						estadoMovimiento=MOV_BUSCANDOCASA;

					}
					break;

				default:
					MandaRespuesta("FindHome", "ERR_NA");
					ledRojo.Pulsos(1500,500,3);
					break;
					break;
			}

		break;

	default:
		MandaRespuesta("FindHome", "ERR_NA");
		ledRojo.Pulsos(1500,500,3);
		break;
	}

}

// Funcion que devuelva el azimut actual de la cupula
long GetCurrentAzimut() {

	// Si los pasòs totales del stepper son mas o igual de los maximos (mas de 360º) restamos una vuelta. Luego en el loop cuando estemos parados corregiremos el valor de la libreria stepper
	
	long t_pasos = controladorStepper.currentPosition();
	if ( t_pasos < TotalPasos && t_pasos >= 0 ) {

		return PasosToGrados(t_pasos);

	}

	else if (t_pasos >= TotalPasos && t_pasos >= 0) {

		return PasosToGrados(t_pasos - TotalPasos);

	}

	else if (t_pasos <= 0)
	{

		return PasosToGrados(TotalPasos + t_pasos);

	}
		
	else {

		return 0;

	}

}

// Funcion para mover la cupula a una posicion Azimut concreta
void MoveTo(int grados) {


	switch (estadoMecanica){

		case MEC_NOT_INIT:
			MandaRespuesta("SlewToAZimut", "ERR_HNI");
			ledRojo.Pulsos(1500,500,3);
			break;
			
		case MEC_EMERGENCY:
			MandaRespuesta("SlewToAZimut", "ERR_EME");
			ledRojo.Pulsos(1500,500,3);
			break;

		case MEC_INITIALIZING: case MEC_OK:

			switch (estadoMovimiento){

				case MOV_OKSLEWING:
					MandaRespuesta("SlewToAZimut", "ERR_SLW");
					ledRojo.Pulsos(1500,500,3);
					break;
				
				case MOV_OKSTOPPED: case MOV_BUSCANDOCASA: case MOV_APARCANDO:

					// Verificar que los grados no estan fuera de rango
					if (grados >= 0 && grados <= 359) {
						
						// Si es aqui el comando es OK, nos movemos.
						// movimiento relativo a realiar
						int delta = (grados - GetCurrentAzimut());
										
						// Aqui el algoritmo para movernos con las opciones que tenemos de la libreria del Stepper (que no esta pensada para un "anillo" como es la cupula)
						
						int amover = 0;
						

						if (delta >= -180 && delta <= 180) {

							amover = delta;
						
						}

						else if (delta > 180) {

							amover = delta - 360;

						}
						
						else if (delta < 180) {

							amover = delta + 360;
							
						}
						
						MandaRespuesta("SlewToAZimut", "CMD_OK");

						if(estadoMovimiento==MOV_OKSTOPPED){

							estadoMovimiento=MOV_OKSLEWING;
								
						}

						controladorStepper.move(GradosToPasos(amover));
						
					}
			
					break;

				default:
					MandaRespuesta("SlewToAZimut", "ERR_NA");
					ledRojo.Pulsos(1500,500,3);
					break;
					break;
			}


			break;

		default:
			MandaRespuesta("SlewToAZimut", "ERR_NA");
			ledRojo.Pulsos(1500,500,3);
			break;
	}

}

void Connected(){

	MandaRespuesta("Connected", DriverOK?"TRUE":"FALSE");

}

void Connected(boolean status){

	if (status != DriverOK){

		DriverOK = status;

	}

			MandaRespuesta("Connected", DriverOK?"TRUE":"FALSE");

}

void AbortSlew(){

	// Implementar despues la parte del Shutter tambien.

	if (estadoMovimiento!=MOV_OKSTOPPED && estadoMovimiento!=MOV_ERROR){

		MandaRespuesta("AbortSlew", "CMD_OK");
		ledRojo.Pulsos(500,500,2);

		// Parar Stepper de Azimut (se para con aceleracion no a lo burro)
		controladorStepper.stop();

		// Esperar 5 segundos a que se pare el tema y si no error
		int TiempoEspera;
		for (TiempoEspera = 0; TiempoEspera < 5; TiempoEspera++){

			if (controladorStepper.isRunning() == false){

				
				estadoMovimiento=MOV_OKSTOPPED;
				MandaRespuesta("AbortSlew", "STOPPED");
				return;

			}

			// Esto, que es KAKA, aqui no importa, que la Task Procesacomandos se espere que esto es importante
			delay(1000);

		}

	MandaRespuesta("AbortSlew", "ERR");
	estadoMovimiento=MOV_ERROR;
	estadoMecanica=MEC_NOT_INIT;
	
	}
}

void SetPark(int l_ParkPos){

	if (l_ParkPos >= 0 && l_ParkPos <360){

		ParkPos = l_ParkPos;
		HayQueSalvar = true;
		MandaRespuesta("SetPark", "SET_OK");
		
	}

	else {

		MandaRespuesta("SetPark", "ERR_OR");

	}

}

void Park(){

	AbortSlew();
	MoveTo(ParkPos);
	estadoMovimiento=MOV_APARCANDO;

}

void EmergencyStop(){

	ledRojo.Pulsos(1500,500,3);
	AbortSlew();
	estadoMovimiento=MOV_ERROR;
	estadoMecanica=MEC_EMERGENCY;

}

// Esta funcion se lanza desde el loop. Es la que hace las comprobaciones. No debe atrancarse nunca tampoco (ni esta ni ninguna)
void RunCupula() {

	// SWITCH DE EMERGENCIA. MANDA MAS QUE NADIE
	Debouncer_Emergency_Stop.update();
	if(!Debouncer_Emergency_Stop.read() && estadoMecanica!=MEC_EMERGENCY){

		EmergencyStop();
		
	}
	
	// Lectura del sensor de HOME
	Debouncer_HomeSwitch.update();
	AtHome=Debouncer_HomeSwitch.read();

	// AQUI HACER NUEVA MAQUINA DE ESTADO
	switch (estadoMovimiento){
			
		case MOV_APARCANDO:
			/* code */
			break;
		
		case MOV_BUSCANDOCASA:
			
			if(AtHome){

				AbortSlew();
				controladorStepper.setCurrentPosition(0);	// Poner la posicion a cero ("hacer cero")
				MandaRespuesta("FindHome", "ATHOME");		// Responder al comando FINDHOME
							
				if(estadoMecanica==MEC_INITIALIZING){

						MandaRespuesta("InitHW", "READY");	// Responder al comando INITHW
						estadoMecanica=MEC_OK;

				}

			}
			
			/* code */
			break;
		
		case MOV_OKSLEWING:
			/* code */
			break;

		case MOV_OKSTOPPED:
			if (HayQueSalvar){

				SalvaConfig();
				HayQueSalvar = false;

			}
			
			// Si la posicion del stepper en el objeto de su libreria esta fuera de rangos y estoy parado corregir
			if (controladorStepper.currentPosition() > TotalPasos || controladorStepper.currentPosition() < 0) {

				controladorStepper.setCurrentPosition(GradosToPasos(GetCurrentAzimut()));

			}
			break;

		default:
			break;
	}

}

boolean SalvaConfig(){

	File DomoConfigFile = SPIFFS.open("/BMDomo1.json", "w");

	if (!DomoConfigFile) {
		Serial.println("No se puede abrir el fichero de configuracion de la cupula");
		return false;
	}

	else{
	
		if (DomoConfigFile.print(MiEstadoJson(1))){

			DomoConfigFile.close();
			return true;

		}

		else {

			return false;

		}
	}
}

boolean LeeConfig(){

	// Sacar del fichero de configuracion, si existe, las configuraciones permanentes
	if (SPIFFS.exists("/BMDomo1.json")) {

		File DomoConfigFile = SPIFFS.open("/BMDomo1.json", "r");
		if (DomoConfigFile) {
			size_t size = DomoConfigFile.size();
			// Declarar un buffer para almacenar el contenido del fichero
			std::unique_ptr<char[]> buf(new char[size]);
			// Leer el fichero al buffer
			DomoConfigFile.readBytes(buf.get(), size);
			DynamicJsonBuffer jsonBuffer;
			JsonObject& json = jsonBuffer.parseObject(buf.get());
			if (json.success()) {

				Serial.print("Configuracion de la cupula Leida: ");
				json.printTo(Serial);
				Serial.println("");
				ParkPos = json["PRK"].as<int>();
				
				return true;

			}	

			else{

				Serial.println("El fichero de config de la cupula esta corrupto");
				return false;

			}

			

		}

		else{

			Serial.println("No se puede leer el fichero de config de la cupula");
			return false;

		}

	}

	else{

		Serial.println("No se encuentra el fichero de config de la cupula");
		return false;

	}

	
}

#pragma endregion

#pragma region Funciones Auxiliares

// Enviar comandos a la cola de comandos formateados en JSON para ser procesados por la tarea procesa comandos
void enviaComando (String l_comando, String l_payload){

	
	// Formatear el JSON del comando y mandarlo a la cola de comandos.
	DynamicJsonBuffer jsonBuffer;
	JsonObject& ObjJson = jsonBuffer.createObject();
	ObjJson.set("COMANDO",l_comando);
	ObjJson.set("PAYLOAD",l_payload);

	char JSONmessageBuffer[100];
	ObjJson.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
			
	// Mando el comando a la cola de comandos recibidos que luego procesara la tarea manejadordecomandos.
	xQueueSend(colaComandos, &JSONmessageBuffer, 0);

}

#pragma endregion

#pragma region Funciones de gestion de las conexiones Wifi

// Funcion ante un evento de la wifi
void WiFiEventCallBack(WiFiEvent_t event) {
    
	//Serial.printf("[WiFi-event] event: %d\n", event);
    switch(event) {
				
		case SYSTEM_EVENT_STA_START:
			Serial.println("Conexion WiFi: Iniciando ...");
			break;
    	case SYSTEM_EVENT_STA_GOT_IP:
     	   	Serial.print("Conexion WiFi: Conetado. IP: ");
      	  	Serial.println(WiFi.localIP());
			ledVerde.Ciclo(200,200,1000,2);
			clienteNTP.begin();
			if (clienteNTP.update()){

				Serial.print("Reloj Actualizado via NTP: ");
				Serial.println(clienteNTP.getFormattedTime());
				
			}
			else{

				Serial.println("ERR: No se puede actualizar la hora via NTP");

			}
			
        	break;
    	case SYSTEM_EVENT_STA_DISCONNECTED:
        	Serial.println("Conexion WiFi: Desconetado");
			ledVerde.Ciclo(1000,500,500,1);
        	break;
		default:
			break;

    }
		
}

#pragma endregion

#pragma region Funciones de gestion de las conexiones MQTT

// Manejador del evento de conexion al MQTT
void onMqttConnect(bool sessionPresent) {

	Serial.println("Conexion MQTT: Conectado. Core:" + String(xPortGetCoreID()));
	
	ledVerde.Encender();

	bool susflag = false;
	bool lwtflag = false;

	
	// Suscribirse al topic de Entrada de Comandos
	if (clienteMqtt.subscribe(MiConfig.cmndTopic.c_str(), 2)) {

		// Si suscrito correctamente
		Serial.println("Suscrito al topic " + MiConfig.cmndTopic);

		susflag = true;				

	}
		
	else { Serial.println("Error Suscribiendome al topic " + MiConfig.cmndTopic); }

	
	// Publicar un Online en el LWT
	if (clienteMqtt.publish((MiConfig.teleTopic + "/LWT").c_str(), 2,true,"Online")){

		// Si llegamos hasta aqui es estado de las comunicaciones con WIFI y MQTT es OK
		Serial.println("Publicado Online en Topic LWT: " + (MiConfig.teleTopic + "/LWT"));
		
		lwtflag = true;

	}


	if (!susflag || !lwtflag){

		// Si falla la suscripcion o el envio del Online malo kaka. Me desconecto para repetir el proceso.
		clienteMqtt.disconnect(false);

	}

	else{

		// Si todo ha ido bien, proceso de inicio terminado.
		ComOK = true;
		Serial.print("** ");
		Serial.print(clienteNTP.getFormattedTime());
		Serial.println(" - SISTEMA INICIADO CORRECTAMENTE **");

	}

}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  
	Serial.println("Conexion MQTT: Desconectado");
	//miCuadroMando.ledVerde.Ciclo(200,200,1000,2);

}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
	//Serial.print("Subscripcion Realizada. PacketID: ");
	//Serial.println(packetId);
}

void onMqttUnsubscribe(uint16_t packetId) {
  //Serial.print("Subscricion Cancelada. PacketID: ");
  //Serial.println(packetId);
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  
	String s_topic = String(topic);

		// Para que no casque si no viene payload. Asi todo OK al gestor de comandos le llega vacio como debe ser, el JSON lo pone bien.
		if (payload == NULL){

			payload = "NULL";

		}
	
		// Lo que viene en el char* payload viene de un buffer que trae KAKA, hay que limpiarlo (para eso nos pasan len y tal)
		char c_payload[len+1]; 										// Array para el payload y un hueco mas para el NULL del final
		strlcpy(c_payload, payload, len+1); 			// Copiar del payload el tamaño justo. strcopy pone al final un NULL
		
		// Y ahora lo pasamos a String que esta limpito
		String s_payload = String(c_payload);

		// Sacamos el prefijo del topic, o sea lo que hay delante de la primera /
		int Indice1 = s_topic.indexOf("/");
		String Prefijo = s_topic.substring(0, Indice1);
		
		// Si el prefijo es cmnd se lo mandamos al manejador de comandos
		if (Prefijo == "cmnd") { 

			// Sacamos el "COMANDO" del topic, o sea lo que hay detras de la ultima /
			int Indice2 = s_topic.lastIndexOf("/");
			String Comando = s_topic.substring(Indice2 + 1);

			enviaComando(Comando,s_payload);

		}

	//}

}

void onMqttPublish(uint16_t packetId) {
  
	// Al publicar no hacemos nada de momento.

}

// Manda a la cola de respuestas el mensaje de respuesta. Esta funcion la uso como CALLBACK para el objeto cupula
void MandaRespuesta(String comando, String payload) {

			String t_topic = MiConfig.statTopic + "/" + comando;

			DynamicJsonBuffer jsonBuffer;
			JsonObject& ObjJson = jsonBuffer.createObject();
			// Tipo de mensaje (MQTT, SERIE, BOTH)
			ObjJson.set("TIPO","BOTH");
			// Comando
			ObjJson.set("CMND",comando);
			// Topic (para MQTT)
			ObjJson.set("MQTTT",t_topic);
			// RESPUESTA
			ObjJson.set("RESP",payload);

			char JSONmessageBuffer[300];
			ObjJson.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
			
			// Mando el comando a la cola de comandos recibidos que luego procesara la tarea manejadordecomandos.
			xQueueSend(colaRespuestas, &JSONmessageBuffer, 0); 

}

// envia al topic tele la telemetria en Json
void MandaTelemetria() {
	
	if (clienteMqtt.connected()){

			String t_topic = MiConfig.teleTopic + "/INFO1";

			DynamicJsonBuffer jsonBuffer;
			JsonObject& ObjJson = jsonBuffer.createObject();
			ObjJson.set("TIPO","MQTT");
			ObjJson.set("CMND","TELE");
			ObjJson.set("MQTTT",t_topic);
			ObjJson.set("RESP",MiEstadoJson(1));
			
			char JSONmessageBuffer[300];
			ObjJson.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
			
			// Mando el comando a la cola de comandos recibidos que luego procesara la tarea manejadordecomandos.
			xQueueSendToBack(colaRespuestas, &JSONmessageBuffer, 0); 

	}
	
}

#pragma endregion

#pragma region Funciones Cuadro de Mando de Usuario

void handleClickBoton1(){
		
	enviaComando("InitHW","STD");
	
}


// BOTON1 HOLD
void handleHoldBoton1(){

	enviaComando("InitHW","FORCE");

}


void handleClickBoton2(){
		
	enviaComando("SlewRelativeAzimut","-45");

}

void handleHoldBoton2(){

	enviaComando("SetParkHere","NA");

}

void handleClickBoton3(){

	enviaComando("AbortSlew","NA");

}

void handleHoldBoton3(){
	
	enviaComando("Park","NA");

}

void handleClickBoton4(){

	enviaComando("SlewRelativeAzimut","45");

}

void handleHoldBoton4(){

}


#pragma endregion

#pragma region TASKS


// Tarea para vigilar la conexion con el MQTT y conectar si no estamos conectados
void TaskGestionRed ( void * parameter ) {

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 4000;
	xLastWakeTime = xTaskGetTickCount ();


	while(true){

		if (WiFi.isConnected() && !clienteMqtt.connected()){
			
			clienteMqtt.connect();
			
		}
		
		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}

}

//Tarea para procesar la cola de comandos recibidos
void TaskProcesaComandos ( void * parameter ){

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 100;
	xLastWakeTime = xTaskGetTickCount ();

	char JSONmessageBuffer[100];
	
	while (true){
			
			// Limpiar el Buffer
			memset(JSONmessageBuffer, 0, sizeof JSONmessageBuffer);

			if (xQueueReceive(colaComandos,&JSONmessageBuffer,0) == pdTRUE ){

				//Serial.println("Procesando comando: " + String(JSONmessageBufferRX));

				String COMANDO;
				String PAYLOAD;
				DynamicJsonBuffer jsonBuffer;

				JsonObject& ObjJson = jsonBuffer.parseObject(JSONmessageBuffer);

				//json.printTo(Serial);
				if (ObjJson.success()) {
				
					COMANDO = ObjJson["COMANDO"].as<String>();
					PAYLOAD = ObjJson["PAYLOAD"].as<String>();
					
					Serial.println("Comando: " + COMANDO);
					Serial.println("Payload: " + PAYLOAD);

					// ##### IMPLEMENTACION DE ASCOM
					// Propiedad (digamos metodo) Connected
					if (COMANDO == "Connected") {

						// Ascom Connected
						// Me vendra un true o false o nada??
						

						if (PAYLOAD == "TRUE"){

							Connected(true);

						}
							
						else if (PAYLOAD == "FALSE"){

							Connected(false);

						}

						else if (PAYLOAD == "STATUS"){

							Connected();

						}

					}

						// Metodo SlewToAZimut(double)
						// InvalidValueException Si el valor esta fuera de rango
						// MethodNotImplementedException Si el Domo no admite el metodo (no tiene sentido aqui)
						// Raises an error if Slaved is True, if not supported, if a communications failure occurs, or if the dome can not reach indicated azimuth
					else if (COMANDO == "SlewToAZimut") {

						// Mover la cupula
						// Vamos a tragar con decimales (XXX.XX) pero vamos a redondear a entero con la funcion round().
						MoveTo(round(PAYLOAD.toFloat()));
						

					}

					// Metodo AbortSlew
					// Calling this method will immediately disable hardware slewing (Slaved will become False). Raises an error if a communications failure occurs, or if the command is known to have failed. 
					else if (COMANDO == "AbortSlew"){

						// Esto para la ejecucion unos 5 segundos maximo (si no tenemos seguridad de que todo esta parado) pero como estamos en una TASK para procesar los comandos
						// no solo no importa sino que esta bien que no se procesen mas hasta que haya una respuesta de este
						// comando ya que es muy importante.
						AbortSlew();

					}

					// Metodo FindHome
					// After Home position is established initializes Azimuth to the default value and sets the AtHome flag. Exception if not supported or communications failure. Raises an error if Slaved is True
					else if (COMANDO == "FindHome") {

							
						FindHome();
						ledRojo.Pulsos(1500,500,1);

					}
					
					else if (COMANDO == "SetPark"){

						SetPark(PAYLOAD.toInt());
						
					}
					

					else if (COMANDO == "Park"){

						Park();
						
					}

						
					// ##### COMANDOS FUERA DE ASCOM
					else if (COMANDO == "InitHW") {

						IniciaCupula(PAYLOAD);

					}

					else if (COMANDO == "SetParkHere"){

						SetPark(GetCurrentAzimut());
						
					}

					else if (COMANDO == "Reboot"){

						ESP.restart();
						
					}

					else if (COMANDO == "SlewRelativeAzimut"){
						
						
						if (!Slewing){

							long destino;
							destino = (GetCurrentAzimut() + round(PAYLOAD.toFloat())) ;

							if(destino >=360 ){

								MoveTo(destino - 360);

							}

							else if(destino < 0){

								MoveTo(destino + 360);

							}

							else {

								MoveTo(destino);

							}

						}
						
					}

					// ##### COMANDOS PARA LA GESTION DE LA CONFIGURACION

					else if (COMANDO == "WSsid"){
						
						String(PAYLOAD).toCharArray(MiConfig.Wssid, sizeof(MiConfig.Wssid));
						Serial.println("Wssid OK: " + PAYLOAD);

					}

					else if (COMANDO == "WPasswd"){

						String(PAYLOAD).toCharArray(MiConfig.WPasswd, sizeof(MiConfig.WPasswd));
						Serial.println("Wpasswd OK: " + PAYLOAD);
						
					}

					else if (COMANDO == "MQTTSrv"){

						String(PAYLOAD).toCharArray(MiConfig.mqttserver, sizeof(MiConfig.mqttserver));
						Serial.println("MQTTSrv OK: " + PAYLOAD);

					}

					else if (COMANDO == "MQTTPort"){

						String(PAYLOAD).toCharArray(MiConfig.mqttport, sizeof(MiConfig.mqttport));
						Serial.println("MQTTPort OK: " + PAYLOAD);

					}

					else if (COMANDO == "MQTTUser"){

						String(PAYLOAD).toCharArray(MiConfig.mqttusuario, sizeof(MiConfig.mqttusuario));
						Serial.println("MQTTUser OK: " + PAYLOAD);

					}

					else if (COMANDO == "MQTTPasswd"){

						String(PAYLOAD).toCharArray(MiConfig.mqttpassword, sizeof(MiConfig.mqttpassword));
						Serial.println("MQTTPasswd OK: " + PAYLOAD);

					}

					else if (COMANDO == "MQTTTopic"){

						String(PAYLOAD).toCharArray(MiConfig.mqtttopic, sizeof(MiConfig.mqtttopic));
						Serial.println("MQTTTopic OK: " + PAYLOAD);

					}

					else if (COMANDO == "SaveCom"){

						if (MiConfig.escribeconfig()){

							clienteMqtt.setServer(MiConfig.mqttserver, 1883);
							clienteMqtt.setCredentials(MiConfig.mqttusuario,MiConfig.mqttpassword);
							clienteMqtt.setWill(MiConfig.lwtTopic.c_str(),2,true,"Offline");
							WiFi.begin(MiConfig.Wssid, MiConfig.WPasswd);

						}
						
					}

					// Y Ya si no es de ninguno de estos grupos ....

					else {

						Serial.println("Me ha llegado un comando que no entiendo");


					}

				}

				else {

						Serial.println("Me ha llegado un comando que no puedo Deserializar: ");
						
				}
			
			}
		
		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}

}

// Tarea para procesar la cola de respuestas
void TaskEnviaRespuestas( void * parameter ){

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 100;
	xLastWakeTime = xTaskGetTickCount ();
	
	char JSONmessageBuffer[300];
	

	while(true){

		// Limpiar el Buffer
		memset(JSONmessageBuffer, 0, sizeof JSONmessageBuffer);

		if (xQueueReceive(colaRespuestas,&JSONmessageBuffer,0) == pdTRUE ){

				DynamicJsonBuffer jsonBuffer;

				JsonObject& ObjJson = jsonBuffer.parseObject(JSONmessageBuffer);

				if (ObjJson.success()) {
					
					String TIPO = ObjJson["TIPO"].as<String>();
					String CMND = ObjJson["CMND"].as<String>();
					String MQTTT = ObjJson["MQTTT"].as<String>();
					String RESP = ObjJson["RESP"].as<String>();
					
					if (TIPO == "BOTH"){

						clienteMqtt.publish(MQTTT.c_str(), 2, false, RESP.c_str());
						Serial.println(clienteNTP.getFormattedTime() + " " + CMND + " " + RESP);
						
					}

					else 	if (TIPO == "MQTT"){

						clienteMqtt.publish(MQTTT.c_str(), 2, false, RESP.c_str());
																								
					}
					
					else 	if (TIPO == "SERIE"){

							Serial.println(clienteNTP.getFormattedTime() + " " + CMND + " " + RESP);
						
					}
						
				}
		}

		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}

}

// Tarea para el metodo run del objeto de la cupula.
void TaskCupulaRun( void * parameter ){

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 100;
	xLastWakeTime = xTaskGetTickCount ();
	
	while(true){

		RunCupula();

		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}

}

// Tarea para los comandos que llegan por el puerto serie
void TaskComandosSerieRun( void * parameter ){

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 100;
	xLastWakeTime = xTaskGetTickCount ();

	char sr_buffer[120];
	int16_t sr_buffer_len(sr_buffer!=NULL && sizeof(sr_buffer) > 0 ? sizeof(sr_buffer) - 1 : 0);
	int16_t sr_buffer_pos = 0;
	char* sr_term = "\r\n";
	char* sr_delim = " ";
	int16_t sr_term_pos = 0;
	char* sr_last_token;
	char* comando = "NA";
	char* parametro1 = "NA";

	while(true){
		
		while (Serial.available()) {

			// leer un caracter del serie (en ASCII)
			int ch = Serial.read();

			// Si es menor de 0 es KAKA
			if (ch <= 0) {
				
				continue;
			
			}

			// Si el buffer no esta lleno, escribir el caracter en el buffer y avanzar el puntero
			if (sr_buffer_pos < sr_buffer_len){
			
				sr_buffer[sr_buffer_pos++] = ch;
				//Serial.println("DEBUG: " + String(sr_buffer));

			}
		
			// Si esta lleno ........
			else { 

				return;

			}

			// Aqui para detectar el retorno de linea
			if (sr_term[sr_term_pos] != ch){
				sr_term_pos = 0;
				continue;
			}

			// Si hemos detectado el retorno de linea .....
			if (sr_term[++sr_term_pos] == 0){

				sr_buffer[sr_buffer_pos - strlen(sr_term)] = '\0';

				// Aqui para sacar cada una de las "palabras" del comando que hemos recibido con la funcion strtok_r (curiosa funcion)
				comando = strtok_r(sr_buffer, sr_delim, &sr_last_token);
				parametro1 = strtok_r(NULL, sr_delim, &sr_last_token);

				enviaComando(comando,parametro1);
				
				// Reiniciar los buffers
				sr_buffer[0] = '\0';
				sr_buffer_pos = 0;
				sr_term_pos = 0;
				
			}
		

		}
	
		
		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}
	
}

// tarea para el envio periodico de la telemetria
void TaskMandaTelemetria( void * parameter ){

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 1000;
	xLastWakeTime = xTaskGetTickCount ();
	

	while(true){

		MandaTelemetria();
		
		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}
	
}

// tarea para gestion del cuadro de mando
void TaskGestionCuadro( void * parameter ){

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 5;
	xLastWakeTime = xTaskGetTickCount ();
	

	while(true){

		if (HardwareOK){
			

			if (Slewing){

				if (ledAzul.EstadoLed == IndicadorLed::TipoEstadoLed::LED_ENCENDIDO){

					ledAzul.Ciclo(250,125,125,1);

				}
				

			}

			else {			
			
				ledAzul.Encender();	
								
			}

		}
		
		else{

			if (ledAzul.EstadoLed != IndicadorLed::LED_APAGADO){

				ledAzul.Apagar();

			}
			

		}
		
		boton1.tick();
		boton2.tick();
		boton3.tick();
		boton4.tick();
		ledRojo.RunFast();
		ledVerde.RunFast();
		ledAzul.RunFast();

		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}
	
}

// ISR del timer_stp

uint32_t cp0_regs[18];

void IRAM_ATTR timer_stp_isr() {

	portENTER_CRITICAL(&timerMux);

	 // get FPU state
  uint32_t cp_state = xthal_get_cpenable();
  
  if(cp_state) {
    // Save FPU registers
    xthal_save_cp0(cp0_regs);
  } 
	
	else {
    // enable FPU
    xthal_set_cpenable(1);
  }
	
  
	controladorStepper.run();
	

 	if(cp_state) {
    // Restore FPU registers
    xthal_restore_cp0(cp0_regs);
  } 
	
	else {
    // turn it back off
    xthal_set_cpenable(0);
  }

	portEXIT_CRITICAL(&timerMux);

}


#pragma endregion

#pragma region Funcion Setup() de ARDUINO

// funcion SETUP de Arduino
void setup() {
	
	// Puerto Serie
	Serial.begin(115200);
	Serial.println();

	Serial.println("-- Iniciando Controlador Azimut --");

	InitObjCupula();

	if (MODO_INSTALADOR){

		Serial.println("ATENCION: MODO INSTALADOR ACTIVO");
		Serial.println("FUNCIONALIDAD LIMITADA");

	}

	// Parametros para los botones
  	boton1.setDebounceTicks(50); // ms de debounce
  	boton1.setPressTicks(500); // ms para HOLD
	boton2.setDebounceTicks(50); // ms de debounce
  	boton2.setPressTicks(500); // ms para HOLD
	boton3.setDebounceTicks(50); // ms de debounce
  	boton3.setPressTicks(500); // ms para HOLD
	boton4.setDebounceTicks(50); // ms de debounce
  	boton4.setPressTicks(500); // ms para HOLD

	boton1.attachClick(handleClickBoton1);
	boton1.attachLongPressStart(handleHoldBoton1);
	boton2.attachClick(handleClickBoton2);
	boton2.attachLongPressStart(handleHoldBoton2);
	boton3.attachClick(handleClickBoton3);
	boton3.attachLongPressStart(handleHoldBoton3);
	boton4.attachClick(handleClickBoton4);
	boton4.attachLongPressStart(handleHoldBoton4);

	// Comunicaciones
	clienteMqtt = AsyncMqttClient();
	WiFi.onEvent(WiFiEventCallBack);

	// Lez verde al modo Sin conexion red.
	ledVerde.Ciclo(1000,500,500,1);
	// Iniciar la Wifi

	if (!MODO_INSTALADOR){

		WiFi.begin();

	}
	
	// Iniciar el sistema de ficheros y formatear si no lo esta
	spiffStatus = SPIFFS.begin(true);

	if (SPIFFS.begin()){

		Serial.println("Sistema de ficheros montado");

		// Leer la configuracion de Comunicaciones
		if (MiConfig.leeconfig()){

			// Las funciones callback de la libreria MQTT	
			clienteMqtt.onConnect(onMqttConnect);
  			clienteMqtt.onDisconnect(onMqttDisconnect);
  			clienteMqtt.onSubscribe(onMqttSubscribe);
  			clienteMqtt.onUnsubscribe(onMqttUnsubscribe);
  			clienteMqtt.onMessage(onMqttMessage);
  			clienteMqtt.onPublish(onMqttPublish);
  			clienteMqtt.setServer(MiConfig.mqttserver, 1883);
			clienteMqtt.setCleanSession(true);
			clienteMqtt.setClientId("ControlAzimut");
			clienteMqtt.setCredentials(MiConfig.mqttusuario,MiConfig.mqttpassword);
			clienteMqtt.setKeepAlive(4);
			clienteMqtt.setWill(MiConfig.lwtTopic.c_str(),2,true,"Offline");
	
		}

		// Leer configuracion salvada de la Cupula
		LeeConfig();

	}

	else {

		SPIFFS.begin(true);

	}

	// Instanciar el controlador de Stepper.
	controladorStepper = AccelStepper(AccelStepper::DRIVER, MECANICA_STEPPER_PULSEPIN , MECANICA_STEPPER_DIRPIN);
	controladorStepper.disableOutputs();
	controladorStepper.setCurrentPosition(0);
	controladorStepper.setEnablePin(MECANICA_STEPPER_ENABLEPING);
	controladorStepper.setMaxSpeed(MECANICA_STEPPER_MAXSPEED);
	controladorStepper.setAcceleration(MECANICA_STEPPER_MAXACELERATION);
	controladorStepper.setPinsInverted(MECANICA_STEPPER_INVERTPINS, MECANICA_STEPPER_INVERTPINS, MECANICA_STEPPER_INVERTPINS);
	controladorStepper.setMinPulseWidth(MECANICA_STEPPER_ANCHO_PULSO); // Ancho minimo de pulso en microsegundos

	if (MODO_INSTALADOR){

		controladorStepper.setMaxSpeed(MECANICA_STEPPER_MAXSPEED_INST);
		controladorStepper.setAcceleration(MECANICA_STEPPER_MAXACELERATION_INST);

	}
	

	// COLAS
	colaComandos = xQueueCreate(10,100);
	colaRespuestas = xQueueCreate(10,300);

	// TASKS
	Serial.println("Creando tareas del sistema.");
	
	// Tareas CORE0 gestinadas por FreeRTOS

	xTaskCreatePinnedToCore(TaskGestionCuadro,"GestionCuadro",2000,NULL,2,&tHandleTaskEnviaRespuestas,0);
	xTaskCreatePinnedToCore(TaskEnviaRespuestas,"EnviaMQTT",2000,NULL,1,&tHandleTaskEnviaRespuestas,0);
	xTaskCreatePinnedToCore(TaskProcesaComandos,"ProcesaComandos",3000,NULL,1,&tHandleTaskProcesaComandos,0);
	xTaskCreatePinnedToCore(TaskCupulaRun,"CupulaRun",2000,NULL,1,&tHandleTaskCupulaRun,0);
	xTaskCreatePinnedToCore(TaskComandosSerieRun,"ComandosSerieRun",1000,NULL,1,&tHandleTaskComandosSerieRun,0);

	if (!MODO_INSTALADOR){

		xTaskCreatePinnedToCore(TaskGestionRed,"MQTT_Conectar",3000,NULL,1,&tHandleTaskGestionRed,0);
		xTaskCreatePinnedToCore(TaskMandaTelemetria,"MandaTelemetria",2000,NULL,1,&tHandleTaskMandaTelemetria,0);
	
	}
	
	// Tareas CORE1. 


	// Timers

	timer_stp = timerBegin(0, 80, true);
  	timerAttachInterrupt(timer_stp, &timer_stp_isr, true);
  	timerAlarmWrite(timer_stp, 150, true);
  	timerAlarmEnable(timer_stp);

	if (MODO_INSTALADOR){

		//Serial.println("Probando el panel del Cuadro Electrico");
		//miCuadroMando.TestSalidas();
		Serial.println("ATENCION: MODO INSTALADOR ACTIVO");
		Serial.println("FUNCIONALIDAD LIMITADA");

	}

	// Init Completado.
	Serial.println("Setup Completado.");
	ledRojo.Pulsos(50,50,3);
	
	
}

#pragma endregion

#pragma region Funcion Loop() de ARDUINO

// Funcion LOOP de Arduino. Vacia. Todo en TASKS
void loop() {
	
}

#pragma endregion


/// FIN DEL PROGRAMA ///