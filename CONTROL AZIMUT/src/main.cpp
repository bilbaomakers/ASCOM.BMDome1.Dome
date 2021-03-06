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

#pragma endregion

#pragma region Constantes y configuracion. Modificable aqui por el usuario

#define CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU1 0

// Para la configuracion de conjunto Mecanico de arrastre
static const uint8_t MECANICA_STEPPER_PULSEPIN = 32;					// Pin de pulsos del stepper
static const uint8_t MECANICA_STEPPER_DIRPIN = 25;						// Pin de direccion del stepper
static const uint8_t MECANICA_STEPPER_ENABLEPING = 33;				// Pin de enable del stepper
static const short MECANICA_PASOS_POR_VUELTA_MOTOR = 400;		// Numero de pasos por vuelta del STEPPER (Configuracion del controlador)
static const float MECANICA_STEPPER_MAXSPEED = (MECANICA_PASOS_POR_VUELTA_MOTOR * 5);	// Velocidad maxima del stepper (pasos por segundo)
static const float MECANICA_STEPPER_MAXACELERAION = (MECANICA_STEPPER_MAXSPEED / 3);	// Aceleracion maxima del stepper (pasos por segundo2). Aceleraremos al VMAX en 3 vueltas del motor.
static const short MECANICA_RATIO_REDUCTORA = 6;							// Ratio de reduccion de la reductora
static const short MECANICA_DIENTES_PINON_ATAQUE = 16;				// Numero de dientes del piños de ataque
static const short MECANICA_DIENTES_CREMALLERA_CUPULA = 981;	// Numero de dientes de la cremallera de la cupula
static const boolean MECANICA_STEPPER_INVERTPINS = false;			// Invertir la logica de los pines de control (pulso 1 o pulso 0)
static const int MECANICA_STEPPER_ANCHO_PULSO = 10;						// Ancho de los pulsos

// Otros sensores
static const uint8_t MECANICA_SENSOR_HOME = 26;								// Pin para el sensor de HOME

// Para el ticker del BMDomo1
unsigned long TIEMPO_TICKER_RAPIDO = 500;

// Para la zona horaria (horas de diferencia con UTC)
static const int HORA_LOCAL = 2;

#pragma endregion

#pragma region Objetos

// Para la conexion MQTT
AsyncMqttClient  ClienteMQTT;

// Controlador Stepper
AccelStepper ControladorStepper;

// Los manejadores para las tareas. El resto de las cosas que hace nuestro controlador que son un poco mas flexibles que la de los pulsos del Stepper
TaskHandle_t THandleTaskCupulaRun,THandleTaskProcesaComandos,THandleTaskComandosSerieRun,THandleTaskMandaTelemetria,THandleTaskGestionRed,THandleTaskEnviaRespuestas;	

// Manejadores Colas para comunicaciones inter-tareas
QueueHandle_t ColaComandos,ColaRespuestas;

// Timer Stepper Run
hw_timer_t * timer_stp = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// Flag para el estado del sistema de ficheros
boolean SPIFFStatus = false;

// Conexion UDP para la hora
WiFiUDP UdpNtp;

// Manejador del NTP. Cliente red, servidor, offset zona horaria, intervalo de actualizacion.
// FALTA IMPLEMENTAR ALGO PARA CONFIGURAR LA ZONA HORARIA
NTPClient ClienteNTP(UdpNtp, "europe.pool.ntp.org", HORA_LOCAL * 3600, 3600);

// Para el sensor de temperatura de la CPU. Definir aqui asi necesario es por no estar en core Arduino.
extern "C" {uint8_t temprature_sens_read();}

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
		char mqttpassword[19];

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

#pragma region CLASE BMDomo1 - Clase principial para el objeto que representa la cupula, sus estados, propiedades y acciones

// Una clase tiene 2 partes:
// La primera es la definicion de todas las propiedades y metodos, publicos o privados.
// La segunda es la IMPLEMENTACION de esos metodos o propiedades (que hacen). En C++ mas que nada de los METODOS (que son basicamente funciones)

// Definicion
class BMDomo1{

#pragma region DEFINICIONES BMDomo1
private:

	// Variables Internas para uso de la clase
	bool Inicializando;						// Para saber que estamos ejecutando el comando INITHW
	bool BuscandoCasa;						// Para saber que estamos ejecutando el comando FINDHOME
	bool Aparcando;								// Para saber si estamos yendo a aparcar
	float TotalPasos;							// Variable para almacenar al numero de pasos totales para 360º (0) al iniciar el objeto.
	Bounce Debouncer_HomeSwitch = Bounce(); // Objeto debouncer para el switch HOME
	bool HayQueSalvar;
		
	// Funciones Callback. Son funciones "especiales" que yo puedo definir FUERA de la clase y disparar DENTRO (GUAY).
	// Por ejemplo "una funcion que envie las respuestas a los comandos". Aqui no tengo por que decir ni donde ni como va a enviar esas respuestas.
	// Solo tengo que definirla y cuando cree el objeto de esta clase en mi programa, creo la funcion con esta misma estructura y se la "paso" a la clase
	// que la usara como se usa cualquier otra funcion y ella sabra que hacer

	typedef void(*RespondeComandoCallback)(String comando, String respuesta);			// Definir como ha de ser la funcion de Callback (que le tengo que pasar y que devuelve)
	RespondeComandoCallback MiRespondeComandos = nullptr;								// Definir el objeto que va a contener la funcion que vendra de fuera AQUI en la clase.

	long GradosToPasos(long grados);													// Convierte Grados a Pasos segun la mecanica
	long PasosToGrados(long pasos);														// Convierte pasos a grados segun la mecanica
	
	boolean SalvaConfig();

public:

	BMDomo1(uint8_t PinHome);		// Constructor (es la funcion que devuelve un Objeto de esta clase)
	~BMDomo1() {};	// Destructor (Destruye el objeto, o sea, lo borra de la memoria)


	//  Variables Publicas
	String HardwareInfo;				// Identificador del HardWare y Software
	bool ComOK;									// Si la wifi y la conexion MQTT esta OK
	bool DriverOK;							// Si estamos conectados al driver del PC y esta OK
	bool HardwareOK;						// Si nosotros estamos listos para todo (para informar al driver del PC)
	bool Slewing;								// Si alguna parte del Domo esta moviendose
	bool AtHome;								// Si esta parada en HOME
	bool AtPark;								// Si esta en la posicion de PARK									
	int ParkPos;								// Para almacenar la posicion de Park en la clase

	// Funciones Publicas
	String MiEstadoJson(int categoria);								// Devuelve un JSON con los estados en un array de 100 chars (la libreria MQTT no puede con mas de 100)
	void MoveTo(int azimut);										// Mover la cupula a un azimut
	void IniciaCupula(String parametros);							// Inicializar la cupula
	void ApagaCupula(String parametros);							// Apagar la cupula normalmente.
	void FindHome();												// Mueve la cupula a Home
	long GetCurrentAzimut();									    // Devuelve el azimut actual de la cupula
	void Run();														// Actualiza las propiedades de estado de este objeto en funcion del estado de motores y sensores
	void SetRespondeComandoCallback(RespondeComandoCallback ref);	// Definir la funcion para pasarnos la funcion de callback del enviamensajes
	void Connected();					// Para implementar la propiedad-metodo Connected
	void Connected(boolean status);					// Para implementar la propiedad-metodo Connected
	void AbortSlew();
	void SetPark(int l_ParkPos);
	void Park();
	boolean LeeConfig();

};

#pragma endregion


#pragma region IMPLEMENTACIONES BMDomo1

// Constructor. Lo que sea que haya que hacer antes de devolver el objeto de esta clase al creador.
BMDomo1::BMDomo1(uint8_t PinHome) {	

	HardwareInfo = "BMDome1.HWAz.1.0";
	Inicializando = false;
	ComOK = false;
	DriverOK = false;
	HardwareOK = false;
	Slewing = false;			
	BuscandoCasa = false;
	AtHome = false;
	AtPark = false;
	Aparcando = false;
	TotalPasos = (float)(MECANICA_DIENTES_CREMALLERA_CUPULA * MECANICA_RATIO_REDUCTORA * MECANICA_PASOS_POR_VUELTA_MOTOR) / (float)(MECANICA_DIENTES_PINON_ATAQUE);
	// Inicializacion del sensor de HOMME
	pinMode(PinHome, INPUT_PULLUP);
	Debouncer_HomeSwitch.attach(PinHome);
	Debouncer_HomeSwitch.interval(5);
	ParkPos = 0;
	HayQueSalvar = false;

}

#pragma region Funciones Privadas

// Traduce Grados a Pasos segun la mecanica
long BMDomo1::GradosToPasos(long grados) {

	return round((float)(grados * TotalPasos) / 360);
	
}

// Traduce Pasos a Grados segun la mecanica
long BMDomo1::PasosToGrados(long pasos) {

	// Con una pequeña correccion porque a veces si se pide la posicion por encima de 359.5 devuelve 360 (por el redondeo) y no vale, tiene que ser 0
	long t_grados = round((float)(pasos * 360) / (float)TotalPasos);
	
	if (t_grados == 360) {

		return 0;

	}
		
	else {

		return t_grados;

	}
}

#pragma endregion



#pragma region Funciones Publicas


// Pasar a esta clase la funcion callback de fuera. Me la pasan desde el programa con el metodo SetRespondeComandoCallback
void BMDomo1::SetRespondeComandoCallback(RespondeComandoCallback ref) {

	MiRespondeComandos = (RespondeComandoCallback)ref;

}

// Metodo que devuelve un JSON con el estado
String BMDomo1::MiEstadoJson(int categoria) {

	DynamicJsonBuffer jBuffer;
	JsonObject& jObj = jBuffer.createObject();

	// Dependiendo del numero de categoria en la llamada devolver unas cosas u otras
	switch (categoria)
	{

	case 1:

		// Esto llena de objetos de tipo "pareja propiedad valor"
		jObj.set("TIME", ClienteNTP.getFormattedTime());							// HORA
		jObj.set("HI", HardwareInfo);												// Info del Hardware
		jObj.set("CS", ComOK);														// Info de la conexion WIFI y MQTT
		jObj.set("DS", DriverOK);													// Info de la comunicacion con el DRIVER ASCOM (o quiza la cambiemos para comunicacion con "cualquier" driver, incluido uno nuestro
		jObj.set("HS", HardwareOK);													// Info del estado de inicializacion de la mecanica
		jObj.set("AZ", GetCurrentAzimut());											// Posicion Actual (AZ)
		jObj.set("ATH", AtHome); 													// Propiedad AtHome
		jObj.set("PRK", ParkPos);													// Posicion almacenada de Park
		jObj.set("ATPRK", AtPark);													// Propiedad AtPark
		jObj.set("POS", ControladorStepper.currentPosition());  					// Posicion en pasos del objeto del Stepper
		jObj.set("TOT", TotalPasos);												// Numero total de pasos por giro de la cupula
		jObj.set("MAXT", ControladorStepper.maxexectime());							// Tiempo de ejecucion maxima del ciclo Stepper
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
void BMDomo1::IniciaCupula(String parametros) {

	// Esto es para inicializar forzado si le pasamos parametro FORCE
	if (parametros == "FORCE") {

		HardwareOK = true;
		MiRespondeComandos("InitHW", "READY");
		ControladorStepper.enableOutputs();
		
	}

	else if (parametros == "STD"){


		Inicializando = true;

		// Activar el motor. el resto del objeto Stepper ya esta OK
		ControladorStepper.enableOutputs();

		// Como el metodo ahora mismo es dar toda la vuelta, hago cero. Si cambio el metodo esto no valdria.
		ControladorStepper.setCurrentPosition(0);

		// Busca Home (Asyncrono)
		FindHome();

	}

	
}

// Funcion que da orden de mover la cupula para esperar HOME.
void BMDomo1::FindHome() {

	
	// Si la cupula no esta inicializada no hacemos nada mas que avisar
	if (!HardwareOK && !Inicializando) {

		MiRespondeComandos("FindHome", "ERR_HNI");
		
	}
	
	else {

	
		// Primero Verificar si estamos en Home y parados porque entonces estanteria hacer na si ya esta todo hecho .....
		if (!Slewing && AtHome) {

			// Caramba ya estamos en Home y Parados No tenemos que hacer nada
			MiRespondeComandos("FindHome", "ATHOME");
			return;

		}

		// Si doy esta orden pero la cupula se esta moviendo .......
		else if (Slewing) {

		
			// Que nos estamos moviendo leñe no atosigues .....
			MiRespondeComandos("FindHome", "ERR_SLW");
			return;

		}
	
		// Y si estamos parados y NO en Home .....
		else if (!Slewing && !AtHome){

			MiRespondeComandos("FindHome", "CMD_OK");

			// Pues aqui si que hay que movernos hasta que encontremos casa y pararnos.
			// Aqui nos movemos (de momento a lo burro a dar media vuelta entera)
			MoveTo(0);

			// Pero tenemos que salir de la funcion esta no podemos estar esperando a que de la vuelta asi que activo el flag para saber que "estoy buscando home" y termino
			BuscandoCasa = true;
	
		}

	}

}

// Funcion que devuelva el azimut actual de la cupula
long BMDomo1::GetCurrentAzimut() {

	// Si los pasòs totales del stepper son mas o igual de los maximos (mas de 360º) restamos una vuelta. Luego en el loop cuando estemos parados corregiremos el valor de la libreria stepper
	
	long t_pasos = ControladorStepper.currentPosition();
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
void BMDomo1::MoveTo(int grados) {


	// Si la cupula no esta inicializada no hacemos nada mas que avisar
	if (!HardwareOK && !Inicializando) {

		// Error Hardware Not Init
		MiRespondeComandos("SlewToAZimut", "ERR_HNI");
		
	}

	else if (Slewing) {


		MiRespondeComandos("SlewToAZimut", "ERR_SLW");

	}

	else {

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
			
			MiRespondeComandos("SlewToAZimut", "CMD_OK");
			
			ControladorStepper.move(GradosToPasos(amover));
				


		}

		else {
			
			MiRespondeComandos("SlewToAZimut", "ERR_OR");

		}
		

	}
	

}

void BMDomo1::Connected(){

	MiRespondeComandos("Connected", DriverOK?"TRUE":"FALSE");

}

void BMDomo1::Connected(boolean status){

	if (status != DriverOK){

		DriverOK = status;

	}

			MiRespondeComandos("Connected", DriverOK?"TRUE":"FALSE");

}

void BMDomo1::AbortSlew(){

	// Implementar despues la parte del Shutter tambien.

	MiRespondeComandos("AbortSlew", "CMD_OK");

	// Parar Stepper de Azimut (se para con aceleracion no a lo burro)
	ControladorStepper.stop();

	// Esperar 5 segundos a que se pare el tema y si no error
	int TiempoEspera;
	for (TiempoEspera = 0; TiempoEspera < 5; TiempoEspera++){

		if (ControladorStepper.isRunning() == false){

			// Por si hemos parado estando aparcando o buscando casa
			Aparcando = false;
			BuscandoCasa = false;

			// Responder OK.
			MiRespondeComandos("AbortSlew", "STOPPED");
			return;

		}

		// Esto, que es KAKA, aqui no importa, que la Task Procesacomandos se espere que esto es importante
		delay(1000);

	}

	MiRespondeComandos("AbortSlew", "ERR");
		
}


void BMDomo1::SetPark(int l_ParkPos){

	if (l_ParkPos >= 0 && l_ParkPos <360){

		ParkPos = l_ParkPos;
		HayQueSalvar = true;
		MiRespondeComandos("SetPark", "SET_OK");
		
	}

	else {

		MiRespondeComandos("SetPark", "ERR_OR");

	}

}

void BMDomo1::Park(){

	this->AbortSlew();
	this->MoveTo(this->ParkPos);
	this->Aparcando = true;

}

// Esta funcion se lanza desde el loop. Es la que hace las comprobaciones. No debe atrancarse nunca tampoco (ni esta ni ninguna)
void BMDomo1::Run() {

	// Actualizar la lectura de los switches
	Debouncer_HomeSwitch.update();

	// Una importante es actualizar la propiedad Slewing con el estado del motor paso a paso. 
	Slewing = ControladorStepper.isRunning();

	// Algunas cosas que hacemos en funcion si nos estamos moviendo o no

	if (Slewing) {

		if (!Debouncer_HomeSwitch.read()) {

			// Actualizar la propiedad ATHome
			AtHome = true;

			// Pero es que ademas si esta pulsado, nos estamos moviendo y estamos "buscando casa" .....
			if (BuscandoCasa) {

				ControladorStepper.stop();					// Parar el motor que hemos llegado a HOME
				ControladorStepper.setCurrentPosition(0);	// Poner la posicion a cero ("hacer cero")

				// Aqui como tenemos aceleraciones va a tardar en parar y nos vamos a pasar de home un cacho (afortunadamente un cacho conocido)
				// Tendremos que volver para atras despacio el cacho que nos hemos pasado del Switch. Hay que hacer el calculo.


				BuscandoCasa = false;							// Cambiar el flag interno para saber que "ya no estamos buscando casa, ya hemos llegado"
				MiRespondeComandos("FindHome", "ATHOME");		// Responder al comando FINDHOME

				if (Inicializando) {							// Si ademas estabamos haciendo esto desde el comando INITHW ....

					Inicializando = false;						// Cambiar la variable interna para saber que "ya no estamos inicializando, ya hemos terminado"
					MiRespondeComandos("InitHW", "READY");	// Responder al comando INITHW

				}

			}

		}

		// Y si no esta pulsado .....
		else
		{
			// Actualizamos la propiedad AtHome
			AtHome = false;

		}

	}

	// Esto es si estamos parados
	else {

		// Salvar config si hay que salvar
		// Esto hago asi porque si estamos moviendo el motor me salta un panic al acceder al SPIFFS
		// Esto sera por un tema de interrupciones
		if (HayQueSalvar){

			SalvaConfig();
			HayQueSalvar = false;

		}
		

		// Si la posicion del stepper en el objeto de su libreria esta fuera de rangos y estoy parado corregir
		if (ControladorStepper.currentPosition() > TotalPasos || ControladorStepper.currentPosition() < 0) {

			ControladorStepper.setCurrentPosition(GradosToPasos(GetCurrentAzimut()));

		}

		// Actualizar la propiedad AtPark si estamos parados en el azimut del Park
		if (this->GetCurrentAzimut() == this->ParkPos){
			
			this->AtPark = true;

			if (this->Aparcando == true){

				this->HardwareOK = false;
				this->Aparcando = false;
				MiRespondeComandos("Park","PRK_DONE");
			
			}

		}

		else{

			this->AtPark = false;

		}

	}

		
	// Otra cosa a comprobar es si estamos buscando home y el motor se ha parado sin llegar a hacer lo de arriba
	// Un supuesto raro, pero eso es que no hemos conseguido detectar el switch home
	if (!Slewing && BuscandoCasa) {

		MiRespondeComandos("FindHome", "ERR_HSF");
		BuscandoCasa = false;
		Inicializando = false;
				
	}

}

boolean BMDomo1::SalvaConfig(){

	File DomoConfigFile = SPIFFS.open("/BMDomo1.json", "w");

	if (!DomoConfigFile) {
		Serial.println("No se puede abrir el fichero de configuracion de la cupula");
		return false;
	}

	if (DomoConfigFile.print(MiEstadoJson(1))){

		return true;

	}

	else {

		return false;

	}

}

boolean BMDomo1::LeeConfig(){

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

			return false;

		}

		return false;

	}

	return false;

}

#pragma endregion


#pragma endregion


// Objeto de la clase BMDomo1. Es necesario definirlo aqui debajo de la definicion de clase. No puedo en la region de arriba donde tengo los demas
// Eso es porque la clase usa objetos de fuera. Esto es chapuza pero de momento asi no me lio. Despues todo por referencia y la clase esta debajo de los includes o en una libreria a parte. Asi esntonces estaria OK.
BMDomo1 MiCupula(MECANICA_SENSOR_HOME);


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
			ClienteNTP.begin();
			if (ClienteNTP.update()){

				Serial.print("Reloj Actualizado via NTP: ");
				Serial.println(ClienteNTP.getFormattedTime());
				
			}
			else{

				Serial.println("ERR: No se puede actualizar la hora via NTP");

			}
			
        	break;
    	case SYSTEM_EVENT_STA_DISCONNECTED:
        	Serial.println("Conexion WiFi: Desconetado");
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
	
	bool susflag = false;
	bool lwtflag = false;

	
	// Suscribirse al topic de Entrada de Comandos
	if (ClienteMQTT.subscribe(MiConfig.cmndTopic.c_str(), 2)) {

		// Si suscrito correctamente
		Serial.println("Suscrito al topic " + MiConfig.cmndTopic);

		susflag = true;				

	}
		
	else { Serial.println("Error Suscribiendome al topic " + MiConfig.cmndTopic); }

	
	// Publicar un Online en el LWT
	if (ClienteMQTT.publish((MiConfig.teleTopic + "/LWT").c_str(), 2,true,"Online")){

		// Si llegamos hasta aqui es estado de las comunicaciones con WIFI y MQTT es OK
		Serial.println("Publicado Online en Topic LWT: " + (MiConfig.teleTopic + "/LWT"));
		
		lwtflag = true;

	}


	if (!susflag || !lwtflag){

		// Si falla la suscripcion o el envio del Online malo kaka. Me desconecto para repetir el proceso.
		ClienteMQTT.disconnect(false);

	}

	else{

		// Si todo ha ido bien, proceso de inicio terminado.
		MiCupula.ComOK = true;
		Serial.print("** ");
		Serial.print(ClienteNTP.getFormattedTime());
		Serial.println(" - SISTEMA INICIADO CORRECTAMENTE **");

	}

}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  
	Serial.println("Conexion MQTT: Desconectado.");

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

			DynamicJsonBuffer jsonBuffer;
			JsonObject& ObjJson = jsonBuffer.createObject();
			ObjJson.set("COMANDO",Comando);
			ObjJson.set("PAYLOAD",s_payload);

			char JSONmessageBuffer[100];
			ObjJson.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
			//Serial.println(String(ObjJson.measureLength()));

			// Mando el comando a la cola de comandos recibidos que luego procesara la tarea manejadordecomandos.
			xQueueSend(ColaComandos, &JSONmessageBuffer, 0);
			
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
			xQueueSend(ColaRespuestas, &JSONmessageBuffer, 0); 

}

// envia al topic tele la telemetria en Json
void MandaTelemetria() {
	
	if (ClienteMQTT.connected()){

			String t_topic = MiConfig.teleTopic + "/INFO1";

			DynamicJsonBuffer jsonBuffer;
			JsonObject& ObjJson = jsonBuffer.createObject();
			ObjJson.set("TIPO","MQTT");
			ObjJson.set("CMND","TELE");
			ObjJson.set("MQTTT",t_topic);
			ObjJson.set("RESP",MiCupula.MiEstadoJson(1));
			
			char JSONmessageBuffer[300];
			ObjJson.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
			
			// Mando el comando a la cola de comandos recibidos que luego procesara la tarea manejadordecomandos.
			xQueueSendToBack(ColaRespuestas, &JSONmessageBuffer, 0); 

	}
	
}


#pragma endregion

#pragma region TASKS


// Tarea para vigilar la conexion con el MQTT y conectar si no estamos conectados
void TaskGestionRed ( void * parameter ) {

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 4000;
	xLastWakeTime = xTaskGetTickCount ();


	while(true){

		if (WiFi.isConnected() && !ClienteMQTT.connected()){
			
			ClienteMQTT.connect();
			
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

			if (xQueueReceive(ColaComandos,&JSONmessageBuffer,0) == pdTRUE ){

				//Serial.println("Procesando comando: " + String(JSONmessageBufferRX));

				String COMANDO;
				String PAYLOAD;
				DynamicJsonBuffer jsonBuffer;

				JsonObject& ObjJson = jsonBuffer.parseObject(JSONmessageBuffer);

				//json.printTo(Serial);
				if (ObjJson.success()) {
				
					COMANDO = ObjJson["COMANDO"].as<String>();
					PAYLOAD = ObjJson["PAYLOAD"].as<String>();
					
					// ##### IMPLEMENTACION DE ASCOM
					// Propiedad (digamos metodo) Connected
					if (COMANDO == "Connected") {

						// Ascom Connected
						// Me vendra un true o false o nada??
						

						if (PAYLOAD == "TRUE"){

							MiCupula.Connected(true);

						}
							
						else if (PAYLOAD == "FALSE"){

							MiCupula.Connected(false);

						}

						else if (PAYLOAD == "STATUS"){

							MiCupula.Connected();

						}

					}

						// Metodo SlewToAZimut(double)
						// InvalidValueException Si el valor esta fuera de rango
						// MethodNotImplementedException Si el Domo no admite el metodo (no tiene sentido aqui)
						// Raises an error if Slaved is True, if not supported, if a communications failure occurs, or if the dome can not reach indicated azimuth
					else if (COMANDO == "SlewToAZimut") {

						// Mover la cupula
						// Vamos a tragar con decimales (XXX.XX) pero vamos a redondear a entero con la funcion round().
						MiCupula.MoveTo(round(PAYLOAD.toFloat()));

					}

					// Metodo AbortSlew
					// Calling this method will immediately disable hardware slewing (Slaved will become False). Raises an error if a communications failure occurs, or if the command is known to have failed. 
					else if (COMANDO == "AbortSlew"){

						// Esto para la ejecucion unos 5 segundos maximo (si no tenemos seguridad de que todo esta parado) pero como estamos en una TASK para procesar los comandos
						// no solo no importa sino que esta bien que no se procesen mas hasta que haya una respuesta de este
						// comando ya que es muy importante.
						MiCupula.AbortSlew();

					}

					// Metodo FindHome
					// After Home position is established initializes Azimuth to the default value and sets the AtHome flag. Exception if not supported or communications failure. Raises an error if Slaved is True
					else if (COMANDO == "FindHome") {

							
						MiCupula.FindHome();


					}
					
					else if (COMANDO == "SetPark"){

						MiCupula.SetPark(PAYLOAD.toInt());
						
					}

					else if (COMANDO == "Park"){

						MiCupula.Park();
						
					}

						
					// ##### COMANDOS FUERA DE ASCOM
					else if (COMANDO == "InitHW") {

						MiCupula.IniciaCupula(PAYLOAD);

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

							ClienteMQTT.setServer(MiConfig.mqttserver, 1883);
							ClienteMQTT.setCredentials(MiConfig.mqttusuario,MiConfig.mqttpassword);
							ClienteMQTT.setWill(MiConfig.lwtTopic.c_str(),2,true,"Offline");
							WiFi.begin(MiConfig.Wssid, MiConfig.WPasswd);

						}
						
					}

					// Y Ya si no es de ninguno de estos grupos ....

					else {

						Serial.println("Me ha llegado un comando que no entiendo");
						Serial.println("Comando: " + COMANDO);
						Serial.println("Payload: " + PAYLOAD);

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

		if (xQueueReceive(ColaRespuestas,&JSONmessageBuffer,0) == pdTRUE ){

				DynamicJsonBuffer jsonBuffer;

				JsonObject& ObjJson = jsonBuffer.parseObject(JSONmessageBuffer);

				if (ObjJson.success()) {
					
					String TIPO = ObjJson["TIPO"].as<String>();
					String CMND = ObjJson["CMND"].as<String>();
					String MQTTT = ObjJson["MQTTT"].as<String>();
					String RESP = ObjJson["RESP"].as<String>();
					
					if (TIPO == "BOTH"){

						ClienteMQTT.publish(MQTTT.c_str(), 2, false, RESP.c_str());
						Serial.println(ClienteNTP.getFormattedTime() + " " + CMND + " " + RESP);
						
					}

					else 	if (TIPO == "MQTT"){

						ClienteMQTT.publish(MQTTT.c_str(), 2, false, RESP.c_str());
																								
					}
					
					else 	if (TIPO == "SERIE"){

							Serial.println(ClienteNTP.getFormattedTime() + " " + CMND + " " + RESP);
						
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

		MiCupula.Run();

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

				// Formatear el JSON del comando y mandarlo a la cola de comandos.
				DynamicJsonBuffer jsonBuffer;
				JsonObject& ObjJson = jsonBuffer.createObject();
				ObjJson.set("COMANDO",comando);
				ObjJson.set("PAYLOAD",parametro1);

				char JSONmessageBuffer[100];
				ObjJson.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
			
				// Mando el comando a la cola de comandos recibidos que luego procesara la tarea manejadordecomandos.
				xQueueSend(ColaComandos, &JSONmessageBuffer, 0);
				
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
	
  
	ControladorStepper.run();
	

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

	// Asignar funciones Callback de Micupula
	MiCupula.SetRespondeComandoCallback(MandaRespuesta);
		
	// Comunicaciones
	ClienteMQTT = AsyncMqttClient();
	WiFi.onEvent(WiFiEventCallBack);

	// Iniciar la Wifi
	WiFi.begin();

	// Iniciar el sistema de ficheros y formatear si no lo esta
	SPIFFStatus = SPIFFS.begin(true);

	if (SPIFFS.begin()){

		Serial.println("Sistema de ficheros montado");

		// Leer la configuracion de Comunicaciones
		if (MiConfig.leeconfig()){

			// Las funciones callback de la libreria MQTT	
			ClienteMQTT.onConnect(onMqttConnect);
  			ClienteMQTT.onDisconnect(onMqttDisconnect);
  			ClienteMQTT.onSubscribe(onMqttSubscribe);
  			ClienteMQTT.onUnsubscribe(onMqttUnsubscribe);
  			ClienteMQTT.onMessage(onMqttMessage);
  			ClienteMQTT.onPublish(onMqttPublish);
  			ClienteMQTT.setServer(MiConfig.mqttserver, 1883);
			ClienteMQTT.setCleanSession(true);
			ClienteMQTT.setClientId("ControlAzimut");
			ClienteMQTT.setCredentials(MiConfig.mqttusuario,MiConfig.mqttpassword);
			ClienteMQTT.setKeepAlive(4);
			ClienteMQTT.setWill(MiConfig.lwtTopic.c_str(),2,true,"Offline");
	
		}

		// Leer configuracion salvada de la Cupula
		MiCupula.LeeConfig();

	}

	else {

		SPIFFS.begin(true);

	}

	// Instanciar el controlador de Stepper.
	ControladorStepper = AccelStepper(AccelStepper::DRIVER, MECANICA_STEPPER_PULSEPIN , MECANICA_STEPPER_DIRPIN);
	ControladorStepper.disableOutputs();
	ControladorStepper.setCurrentPosition(0);
	ControladorStepper.setEnablePin(MECANICA_STEPPER_ENABLEPING);
	ControladorStepper.setMaxSpeed(MECANICA_STEPPER_MAXSPEED);
	ControladorStepper.setAcceleration(MECANICA_STEPPER_MAXACELERAION);
	ControladorStepper.setPinsInverted(MECANICA_STEPPER_INVERTPINS, MECANICA_STEPPER_INVERTPINS, MECANICA_STEPPER_INVERTPINS);
	ControladorStepper.setMinPulseWidth(MECANICA_STEPPER_ANCHO_PULSO); // Ancho minimo de pulso en microsegundos

	// COLAS
	ColaComandos = xQueueCreate(10,100);
	ColaRespuestas = xQueueCreate(10,300);

	// TASKS
	Serial.println("Creando tareas del sistema.");
	
	// Tareas CORE0 gestinadas por FreeRTOS
	xTaskCreatePinnedToCore(TaskGestionRed,"MQTT_Conectar",3000,NULL,1,&THandleTaskGestionRed,0);
	xTaskCreatePinnedToCore(TaskProcesaComandos,"ProcesaComandos",3000,NULL,1,&THandleTaskProcesaComandos,0);
	xTaskCreatePinnedToCore(TaskEnviaRespuestas,"EnviaMQTT",2000,NULL,1,&THandleTaskEnviaRespuestas,0);
	xTaskCreatePinnedToCore(TaskCupulaRun,"CupulaRun",2000,NULL,1,&THandleTaskCupulaRun,0);
	xTaskCreatePinnedToCore(TaskMandaTelemetria,"MandaTelemetria",2000,NULL,1,&THandleTaskMandaTelemetria,0);
	xTaskCreatePinnedToCore(TaskComandosSerieRun,"ComandosSerieRun",1000,NULL,1,&THandleTaskComandosSerieRun,0);
	
	// Tareas CORE1. 


	// Timers

	timer_stp = timerBegin(0, 80, true);
  	timerAttachInterrupt(timer_stp, &timer_stp_isr, true);
  	timerAlarmWrite(timer_stp, 150, true);
  	timerAlarmEnable(timer_stp);
	
	// Init Completado.
	Serial.println("Setup Completado.");
	
}

#pragma endregion

#pragma region Funcion Loop() de ARDUINO

// Funcion LOOP de Arduino
void loop() {
		
		//ControladorStepper.run();
	
}

#pragma endregion


/// FIN DEL PROGRAMA ///