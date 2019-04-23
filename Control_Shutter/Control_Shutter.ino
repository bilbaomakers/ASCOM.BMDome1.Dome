
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

#include <SerialCommands.h>				// Libreria para la gestion de comandos por el puerto serie https://github.com/ppedro74/Arduino-SerialCommands
#include <AsyncMqttClient.h>			// Vamos a probar esta que es Asincrona: https://github.com/marvinroger/async-mqtt-client
#include <AccelStepper.h>					// Para controlar el stepper como se merece: https://www.airspayce.com/mikem/arduino/AccelStepper/classAccelStepper.html
#include <FS.h>										// Libreria Sistema de Ficheros
#include <WiFi.h>									// Para las comunicaciones WIFI del ESP32
#include <DNSServer.h>						// La necesita WifiManager para el portal captivo
#include <WebServer.h>						// La necesita WifiManager para el formulario de configuracion (ESP32)
//#include <WiFiManager.h>					// Para la gestion avanzada de la wifi
#include <ArduinoJson.h>					// OJO: Tener instalada una version NO BETA (a dia de hoy la estable es la 5.13.4). Alguna pata han metido en la 6
#include <string>									// Para el manejo de cadenas
#include <Bounce2.h>							// Libreria para filtrar rebotes de los Switches: https://github.com/thomasfredericks/Bounce2
#include <SPIFFS.h>								// Libreria para sistema de ficheros SPIFFS
#include <Wire.h>								// **Librería para control del compás y otros dispositivos one wire
#include <LSM303.h>								// **Librería para control del compás
#include <DHTesp.h>								// **Librería para control del sensor de Temperatura y Humedad DHT11

#pragma endregion

#pragma region Constantes y configuracion. Modificable aqui por el usuario

#define CONFIG_TASK_WDT_CHECK_IDLE_TASK_CPU1 0

// Para la configuracion de conjunto Mecanico de arrastre
//static const uint8_t MECANICA_STEPPER_PULSEPIN = 32;					// Pin de pulsos del stepper
//static const uint8_t MECANICA_STEPPER_DIRPIN = 25;						// Pin de direccion del stepper
//static const uint8_t MECANICA_STEPPER_ENABLEPING = 33;				// Pin de enable del stepper
//static const short MECANICA_PASOS_POR_VUELTA_MOTOR = 400;		// Numero de pasos por vuelta del STEPPER (Configuracion del controlador)
//static const float MECANICA_STEPPER_MAXSPEED = (MECANICA_PASOS_POR_VUELTA_MOTOR * 6);	// Velocidad maxima del stepper (pasos por segundo)
//static const float MECANICA_STEPPER_MAXACELERAION = (MECANICA_STEPPER_MAXSPEED / 3);	// Aceleracion maxima del stepper (pasos por segundo2). Aceleraremos al VMAX en 3 vueltas del motor.
//static const short MECANICA_RATIO_REDUCTORA = 6;							// Ratio de reduccion de la reductora
//static const short MECANICA_DIENTES_PINON_ATAQUE = 16;				// Numero de dientes del piños de ataque
//static const short MECANICA_DIENTES_CREMALLERA_CUPULA = 981;	// Numero de dientes de la cremallera de la cupula
//static const boolean MECANICA_STEPPER_INVERTPINS = false;			// Invertir la logica de los pines de control (pulso 1 o pulso 0)
//static const int MECANICA_STEPPER_ANCHO_PULSO = 20;						// Ancho de los pulsos

// Otros sensores
//static const uint8_t MECANICA_SENSOR_HOME = 26;								// Pin para el sensor de HOME

// Para el ticker del BMDomo1
//unsigned long TIEMPO_TICKER_RAPIDO = 500;


#pragma endregion

#pragma region Variables y estructuras

// Estructura para las configuraciones MQTT
struct MQTTCFG
{

	//Valores c_str para conexion al broker MQTT. Si existen posterioirmente en el fichero de configuracion en el SPIFFS se sobreescibiran con esos valores.
	char mqtt_server[40] = "";
	char mqtt_port[6] = "";
	char mqtt_topic[33] = "";
	char mqtt_usuario[19] = "";
	char mqtt_password[19] = "";

	// Variables internas string para los nombres de los topics. Se les da valor luego al final del setup()
	// El de comandos al que me voy a suscribir para "escuchar".
	String cmndTopic;
	String statTopic;
	String teleTopic;
	String lwtTopic;

} MiConfigMqtt;


#pragma endregion

#pragma region Objetos

// Para la conexion MQTT
AsyncMqttClient  ClienteMQTT;

// Controlador Stepper
//AccelStepper ControladorStepper;


// Los manejadores para las tareas. El resto de las cosas que hace nuestro controlador que son un poco mas flexibles que la de los pulsos del Stepper
TaskHandle_t THandleTaskCupulaRun,THandleTaskProcesaComandos,THandleTaskComandosSerieRun,THandleTaskMandaTelemetria,THandleTaskGestionRed,THandleTaskEnviaRespuestas;	

// Manejadores Colas para comunicaciones inter-tareas
QueueHandle_t ColaComandos,ColaRespuestas;

#pragma endregion

#pragma region CLASE BMDomo1 - Clase principial para el objeto que representa la cupula, sus estados, propiedades y acciones

// Una clase tiene 2 partes:
// La primera es la definicion de todas las propiedades y metodos, publicos o privados.
// La segunda es la IMPLEMENTACION de esos metodos o propiedades (que hacen). En C++ mas que nada de los METODOS (que son basicamente funciones)

// Definicion
class BMDomo1
{

#pragma region DEFINICIONES BMDomo1
private:

	// Variables Internas para uso de la clase
	bool Inicializando;						// Para saber que estamos ejecutando el comando INITHW
	bool BuscandoCasa;						// Para saber que estamos ejecutando el comando FINDHOME
	float TotalPasos;							// Variable para almacenar al numero de pasos totales para 360º (0) al iniciar el objeto.
	Bounce Debouncer_HomeSwitch = Bounce(); // Objeto debouncer para el switch HOME

		
	// Funciones Callback. Son funciones "especiales" que yo puedo definir FUERA de la clase y disparar DENTRO (GUAY).
	// Por ejemplo "una funcion que envie las respuestas a los comandos". Aqui no tengo por que decir ni donde ni como va a enviar esas respuestas.
	// Solo tengo que definirla y cuando cree el objeto de esta clase en mi programa, creo la funcion con esta misma estructura y se la "paso" a la clase
	// que la usara como se usa cualquier otra funcion y ella sabra que hacer

	typedef void(*RespondeComandoCallback)(String comando, String respuesta);			// Definir como ha de ser la funcion de Callback (que le tengo que pasar y que devuelve)
	RespondeComandoCallback MiRespondeComandos = nullptr;								// Definir el objeto que va a contener la funcion que vendra de fuera AQUI en la clase.

	long GradosToPasos(long grados);													// Convierte Grados a Pasos segun la mecanica
	long PasosToGrados(long pasos);														// Convierte pasos a grados segun la mecanica
	

public:

	BMDomo1(uint8_t PinHome);		// Constructor (es la funcion que devuelve un Objeto de esta clase)
	~BMDomo1() {};	// Destructor (Destruye el objeto, o sea, lo borra de la memoria)


	//  Variables Publicas
	String HardwareInfo;						// Identificador del HardWare y Software
	bool ComOK;									// Si la wifi y la conexion MQTT esta OK
	bool DriverOK;								// Si estamos conectados al driver del PC y esta OK
	bool HardwareOK;							// Si nosotros estamos listos para todo (para informar al driver del PC)
	bool Slewing;								// Si alguna parte del Domo esta moviendose
	bool AtHome;								// Si esta parada en HOME
													

	// Funciones Publicas
	String MiEstadoJson(int categoria);								// Devuelve un JSON con los estados en un array de 100 chars (la libreria MQTT no puede con mas de 100)
	void MoveTo(int azimut);										// Mover la cupula a un azimut
	void IniciaCupula(String parametros);							// Inicializar la cupula
	void ApagaCupula(String parametros);							// Apagar la cupula normalmente.
	void FindHome();												// Mueve la cupula a Home
	long GetCurrentAzimut();									    // Devuelve el azimut actual de la cupula
	void Run();														// Actualiza las propiedades de estado de este objeto en funcion del estado de motores y sensores
	void SetRespondeComandoCallback(RespondeComandoCallback ref);	// Definir la funcion para pasarnos la funcion de callback del enviamensajes
		
};

#pragma endregion


#pragma region IMPLEMENTACIONES BMDomo1

// Constructor. Lo que sea que haya que hacer antes de devolver el objeto de esta clase al creador.
BMDomo1::BMDomo1(uint8_t PinHome) {	

	HardwareInfo = "BMDome1.HWAz1.0";
	Inicializando = false;
	ComOK = false;
	DriverOK = false;
	HardwareOK = false;
	Slewing = false;			
	BuscandoCasa = false;
	AtHome = false;
	TotalPasos = (float)(MECANICA_DIENTES_CREMALLERA_CUPULA * MECANICA_RATIO_REDUCTORA * MECANICA_PASOS_POR_VUELTA_MOTOR) / (float)(MECANICA_DIENTES_PINON_ATAQUE);
	// Inicializacion del sensor de HOMME
	pinMode(PinHome, INPUT_PULLUP);
	Debouncer_HomeSwitch.attach(PinHome);
	Debouncer_HomeSwitch.interval(5);

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

	// Esto crea un objeto de tipo JsonObject para el "contenedor de objetos a serializar". De tamaño Objetos + 1
	const int capacity = JSON_OBJECT_SIZE(8);
	StaticJsonBuffer<capacity> jBuffer;
	//DynamicJsonBuffer jBuffer;
	JsonObject& jObj = jBuffer.createObject();

	// Dependiendo del numero de categoria en la llamada devolver unas cosas u otras
	switch (categoria)
	{

	case 1:

		// Esto llena de objetos de tipo "pareja propiedad valor"
		jObj.set("HI", HardwareInfo);														// Info del Hardware
		jObj.set("CS", ComOK);																	// Info de la conexion WIFI y MQTT
		jObj.set("DS", DriverOK);																// Info de la comunicacion con el DRIVER ASCOM (o quiza la cambiemos para comunicacion con "cualquier" driver, incluido uno nuestro
		jObj.set("HS", HardwareOK);															// Info del estado de inicializacion de la mecanica
		jObj.set("AZ", GetCurrentAzimut());											// Posicion Actual (de momento en STEPS)
		jObj.set("POS", ControladorStepper.currentPosition());  // Posicion en pasos del objeto del Stepper
		jObj.set("TOT", TotalPasos);														// Numero total de pasos por giro de la cupula

		break;

	case 2:

		jObj.set("INFO2", "INFO2");							
		
		break;

	default:

		jObj.set("NOINFO", "NOINFO");						// MAL LLAMADO

		break;
	}


	// Crear un buffer (aray de 100 char) donde almacenar la cadena de texto del JSON
	char JSONmessageBuffer[100];

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
		MiRespondeComandos("INITHW", "READY");
		ControladorStepper.enableOutputs();

	}

	else {


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

		MiRespondeComandos("FINDHOME", "HW_NOT_INIT");
		
	}
	
	else {

	
		// Primero Verificar si estamos en Home y parados porque entonces estanteria hacer na si ya esta todo hecho .....
		if (!Slewing && AtHome) {

			// Caramba ya estamos en Home y Parados No tenemos que hacer nada
			MiRespondeComandos("FINDHOME", "ATHOME");
		

		}

		// Si doy esta orden pero la cupula se esta moviendo .......
		else if (Slewing) {

		
			// Que nos estamos moviendo leñe no atosigues .....
			MiRespondeComandos("FINDHOME", "SLEWING");

		}
	
		// Y si estamos parados y NO en Home .....
		else if (!Slewing && !AtHome){

			MiRespondeComandos("FINDHOME", "OK");

			// Pues aqui si que hay que movernos hasta que encontremos casa y pararnos.
			// Aqui nos movemos (de momento a lo burro a dar una vuelta entera)
			MoveTo(359);

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

		MiRespondeComandos("GOTO", "HW_NOT_INIT");
		
	}

	else if (Slewing) {

		MiRespondeComandos("GOTO", "SLEWING");

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
			
			MiRespondeComandos("GOTO", "OK_MOVE_TO: " + String(grados) + " REAL: " + String(amover));
			
			ControladorStepper.move(GradosToPasos(amover));
				


		}

		else {
			
			MiRespondeComandos("GOTO", "OUT_OF_RANGE");

		}
		

	}
	

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
				MiRespondeComandos("FINDHOME", "ATHOME");		// Responder al comando FINDHOME

				if (Inicializando) {							// Si ademas estabamos haciendo esto desde el comando INITHW ....

					Inicializando = false;						// Cambiar la variable interna para saber que "ya no estamos inicializando, ya hemos terminado"
					MiRespondeComandos("INITHW", "READY");	// Responder al comando INITHW

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

	else {

		// Si la posicion del stepper en el objeto de su libreria esta fuera de rangos y estoy parado corregir
		if (ControladorStepper.currentPosition() > TotalPasos || ControladorStepper.currentPosition() < 0) {

			ControladorStepper.setCurrentPosition(GradosToPasos(GetCurrentAzimut()));

		}

	}

		
	// Otra cosa a comprobar es si estamos buscando home y el motor se ha parado sin llegar a hacer lo de arriba
	// Un supuesto raro, pero eso es que no hemos conseguido detectar el switch home
	if (!Slewing && BuscandoCasa) {

		MiRespondeComandos("FINDHOME", "FAILED_HOME_SW_ERROR");
		BuscandoCasa = false;
		Inicializando = false;
				
	}
	
}

#pragma endregion


#pragma endregion


// Objeto de la clase BMDomo1. Es necesario definirlo aqui debajo de la definicion de clase. No puedo en la region de arriba donde tengo los demas
// Eso es porque la clase usa objetos de fuera. Esto es chapuza pero de momento asi no me lio. Despues todo por referencia y la clase esta debajo de los includes o en una libreria a parte. Asi esntonces estaria OK.
BMDomo1 MiCupula(MECANICA_SENSOR_HOME);


#pragma endregion

#pragma region funciones de gestion de la configuracion

// Funcion para leer la configuracion desde el fichero de configuracion
void LeeConfig() {

	// El true es para formatear el sistema de ficheros si falla el montage. Si veo que hace cosas raras mejorar (no hacerlo siempre)
	if (SPIFFS.begin(true)) {
		Serial.println("Sistema de ficheros montado");
		if (SPIFFS.exists("/config.json")) {
			// Si existe el fichero abrir y leer la configuracion y asignarsela a las variables definidas arriba
			Serial.println("Leyendo configuracion del fichero");
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile) {
				Serial.println("Fichero de configuracion encontrado");
				size_t size = configFile.size();
				// Declarar un buffer para almacenar el contenido del fichero
				std::unique_ptr<char[]> buf(new char[size]);
				// Leer el fichero al buffer
				configFile.readBytes(buf.get(), size);
				DynamicJsonBuffer jsonBuffer;
				JsonObject& json = jsonBuffer.parseObject(buf.get());
				//json.printTo(Serial);
				if (json.success()) {
					Serial.println("Configuracion del fichero leida");

					// Leer los valores del MQTT
					strcpy(MiConfigMqtt.mqtt_server, json["mqtt_server"]);
					strcpy(MiConfigMqtt.mqtt_port, json["mqtt_port"]);
					strcpy(MiConfigMqtt.mqtt_topic, json["mqtt_topic"]);
					strcpy(MiConfigMqtt.mqtt_usuario, json["mqtt_usuario"]);
					strcpy(MiConfigMqtt.mqtt_password, json["mqtt_password"]);

				}
				else {
					Serial.println("No se puede carcar la configuracion desde el fichero");
				}
			}
		}
	}
	else {

		Serial.println("No se puede montar el sistema de ficheros");
	}


}

// Funcion para Salvar la configuracion en el fichero de configuracion
void SalvaConfig() {

	Serial.println("Salvando la configuracion en el fichero");
	DynamicJsonBuffer jsonBuffer;
	JsonObject& json = jsonBuffer.createObject();
	json["mqtt_server"] = MiConfigMqtt.mqtt_server;
	json["mqtt_port"] = MiConfigMqtt.mqtt_port;
	json["mqtt_topic"] = MiConfigMqtt.mqtt_topic;
	json["mqtt_usuario"] = MiConfigMqtt.mqtt_usuario;
	json["mqtt_password"] = MiConfigMqtt.mqtt_password;

	File configFile = SPIFFS.open("/config.json", "w");
	if (!configFile) {
		Serial.println("No se puede abrir el fichero de configuracion");
	}

	//json.prettyPrintTo(Serial);
	json.printTo(configFile);
	configFile.close();
	Serial.println("Configuracion Salvada");
	//end save

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
        //FALTA: Conectar al MQTT pero NO se puede desde aqui (el Task de la Wifi me manda a tomar por culo por meterme en su terreno)
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("Conexion WiFi: Desconetado");
        //xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
				//xTimerStart(wifiReconnectTimer, 0);
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
	if (ClienteMQTT.subscribe(MiConfigMqtt.cmndTopic.c_str(), 2)) {

		// Si suscrito correctamente
		Serial.println("Suscrito al topic " + MiConfigMqtt.cmndTopic);

		susflag = true;				

	}
		
	else { Serial.println("Error Suscribiendome al topic " + MiConfigMqtt.cmndTopic); }

	
	// Publicar un Online en el LWT
	if (ClienteMQTT.publish((MiConfigMqtt.teleTopic + "/LWT").c_str(), 2,true,"Online")){

		// Si llegamos hasta aqui es estado de las comunicaciones con WIFI y MQTT es OK
		Serial.println("Publicado Online en Topic LWT: " + (MiConfigMqtt.teleTopic + "/LWT"));
		
		lwtflag = true;

	}


	if (!susflag || !lwtflag){

		// Si falla la suscripcion o el envio del Online malo kaka. Me desconecto para repetir el proceso.
		ClienteMQTT.disconnect(false);

	}

	else{

		// Si todo ha ido bien, proceso de inicio terminado.
		MiCupula.ComOK = true;
		Serial.println("Controlador Azimut Iniciado Correctamente: MQTT-OK");

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

	// CASCA SI NO VIENE PAYLOAD. MEJORAR

	if (payload != NULL){

		//Serial.println("Me ha llegado un comando con payload NULL. Topic: " + String(topic));

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

	}

}

void onMqttPublish(uint16_t packetId) {
  
	// Al publicar no hacemos nada de momento.

}

// Manda a la cola de respuestas el mensaje de respuesta. Esta funcion la uso como CALLBACK para el objeto cupula
void MandaRespuesta(String comando, String payload) {

			String t_topic = MiConfigMqtt.statTopic + "/" + comando;

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

			String t_topic = MiConfigMqtt.teleTopic + "/INFO1";

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

#pragma region Funciones de implementacion de los comandos disponibles por el puerto serie

// Manejadores de los comandos. Aqui dentro lo que queramos hacer con cada comando.
void cmd_WIFI_hl(SerialCommands* sender)
{

	char* parametro1 = sender->Next();
	if (parametro1 == NULL)
	{
		sender->GetSerial()->println("SSID: " + WiFi.SSID() + " Password: " + WiFi.psk());
		return;
	}

	char* parametro2 = sender->Next();
	if (parametro2 == NULL)
	{
		sender->GetSerial()->println("SSID: " + WiFi.SSID() + " Password: " + WiFi.psk());
		return;
	}

	char buffer_ssid[30];
	char buffer_passwd[100];

	String(parametro1).toCharArray(buffer_ssid, sizeof(buffer_ssid));
	String(parametro2).toCharArray(buffer_passwd, sizeof(buffer_passwd));

	sender->GetSerial()->println("Conectando a la Wifi. SSID: " + String(buffer_ssid) + " Password: " + String(buffer_passwd));

	WiFi.begin(buffer_ssid, buffer_passwd);

}


void cmd_MQTTSrv_hl(SerialCommands* sender)
{

	char* parametro = sender->Next();
	if (parametro == NULL)
	{
		sender->GetSerial()->println("MQTTSrv: " + String(MiConfigMqtt.mqtt_server));
		return;
	}

	strcpy(MiConfigMqtt.mqtt_server, parametro);

	sender->GetSerial()->println("MQTTSrv: " + String(parametro));
}


void cmd_MQTTUser_hl(SerialCommands* sender)
{

	char* parametro = sender->Next();
	if (parametro == NULL)
	{
		sender->GetSerial()->println("MQTTUser: " + String(MiConfigMqtt.mqtt_usuario));
		return;
	}

	strcpy(MiConfigMqtt.mqtt_usuario, parametro);

	sender->GetSerial()->println("MQTTUser: " + String(parametro));
}


void cmd_MQTTPassword_hl(SerialCommands* sender)
{

	char* parametro = sender->Next();
	if (parametro == NULL)
	{
		sender->GetSerial()->println("MQTTPassword: " + String(MiConfigMqtt.mqtt_password));
		return;
	}

	strcpy(MiConfigMqtt.mqtt_password, parametro);

	sender->GetSerial()->println("MQTTPassword: " + String(parametro));
}


void cmd_MQTTTopic_hl(SerialCommands* sender)
{

	char* parametro = sender->Next();
	if (parametro == NULL)
	{
		sender->GetSerial()->println("MQTTTopic: " + String(MiConfigMqtt.mqtt_topic));
		return;
	}

	strcpy(MiConfigMqtt.mqtt_topic, parametro);

	sender->GetSerial()->println("MQTTTopic: " + String(parametro));
}


void cmd_SaveConfig_hl(SerialCommands* sender)
{

	SalvaConfig();

}


void cmd_Prueba_hl(SerialCommands* sender)
{

	

}


// Manejardor para comandos desconocidos
void cmd_error(SerialCommands* sender, const char* cmd)
{
	sender->GetSerial()->print("ERROR: No se reconoce el comando [");
	sender->GetSerial()->print(cmd);
	sender->GetSerial()->println("]");
}

// Objetos Comandos
SerialCommand cmd_WIFI("WIFI", cmd_WIFI_hl);
SerialCommand cmd_MQTTSrv("MQTTSrv", cmd_MQTTSrv_hl);
SerialCommand cmd_MQTTUser("MQTTUser", cmd_MQTTUser_hl);
SerialCommand cmd_MQTTPassword("MQTTPassword", cmd_MQTTPassword_hl);
SerialCommand cmd_MQTTTopic("MQTTTopic", cmd_MQTTTopic_hl);
SerialCommand cmd_SaveConfig("SaveConfig", cmd_SaveConfig_hl);
SerialCommand cmd_Prueba("Prueba", cmd_Prueba_hl);

// Buffer para los comandos. Gordote para el comando Wifi que lleva parametros gordos.
char serial_command_buffer_[120];

// Bind del objeto con el puerto serie usando el buffer
SerialCommands serial_commands_(&Serial, serial_command_buffer_, sizeof(serial_command_buffer_), "\r\n", " ");


#pragma endregion

#pragma region TASKS


// Tarea para vigilar la conexion con el MQTT y conectar si no estamos conectados
void TaskGestionRed ( void * parameter ) {

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 10000;
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
					

					if (PAYLOAD.indexOf(" ") >> 0) {
					
						// COMANDO GOTO
						if (COMANDO == "GOTO") {

							// Mover la cupula
							// Vamos a tragar con decimales (XXX.XX) pero vamos a redondear a entero con la funcion round().
							MiCupula.MoveTo(round(PAYLOAD.toFloat()));

						}

						// COMANDO STATUS
						else if (COMANDO == "FINDHOME") {

							
							MiCupula.FindHome();


						}

						// COMANDO STATUS
						else if (COMANDO == "INITHW") {

							MiCupula.IniciaCupula(PAYLOAD);

						}
						
					}

					else {

						Serial.print("Me ha llegado un comando con demasiados parametros: ");
						Serial.println(JSONmessageBuffer);

					}

				

				}

				else {

						Serial.print("Me ha llegado un comando que no puedo Deserializar: ");
						Serial.println(JSONmessageBuffer);

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
					String resp_topic = MiConfigMqtt.teleTopic + "/RESPONSE";

					if (TIPO == "BOTH"){

						ClienteMQTT.publish(MQTTT.c_str(), 2, false, RESP.c_str());
						if (CMND != "TELE") {ClienteMQTT.publish(resp_topic.c_str(), 2, false, RESP.c_str());}
						Serial.println(CMND + " " + RESP);
						
					}

					else 	if (TIPO == "MQTT"){

						ClienteMQTT.publish(MQTTT.c_str(), 2, false, RESP.c_str());
						if (CMND != "TELE") {ClienteMQTT.publish(resp_topic.c_str(), 2, false, RESP.c_str());}
																		
					}
					
					else 	if (TIPO == "SERIE"){

							Serial.println(CMND + " " + RESP);
						
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

	// Añadir los comandos al objeto manejador de comandos serie
	serial_commands_.AddCommand(&cmd_WIFI);
	serial_commands_.AddCommand(&cmd_MQTTSrv);
	serial_commands_.AddCommand(&cmd_MQTTUser);
	serial_commands_.AddCommand(&cmd_MQTTPassword);
	serial_commands_.AddCommand(&cmd_MQTTTopic);
	serial_commands_.AddCommand(&cmd_SaveConfig);
	serial_commands_.AddCommand(&cmd_Prueba);
	
	// Manejador para los comandos Serie no reconocidos.
	serial_commands_.SetDefaultHandler(&cmd_error);

	while(true){
		
		serial_commands_.ReadSerial();
		
		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}
	
}

// tarea para el envio periodico de la telemetria
void TaskMandaTelemetria( void * parameter ){

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 2000;
	xLastWakeTime = xTaskGetTickCount ();
	

	while(true){

		MandaTelemetria();
		
		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}
	
}

#pragma endregion


#pragma region Declaración de variables en COMPUERTA

////////////////////////////////////////////////////////////////////////
//******** Sensor declaration

// Pin assigned to LED
const int ledPin = 13;
// Instancia a las clases Compass
LSM303 compass;
// Pins assigned to IMU Pololu  model: MinIMU9AHRS .......... SDA, SCL, Vin (a 5V) and GRND (ground). Connected to A4, A5, 5V,Gnd in Arduino Uno. 
// In nodemcu and arduino Mega 2560 connect to respective pins.
// SDA pin21 y SCL pin22
// Pin assigned to DHT11 Temperature and Humidity sensor

#define dhtPin 16    //D16 
// El tipo de sensor se define en el set up

 
// Inicializamos el sensor DHT11
DHTesp dht;
TaskHandle_t tempTaskHandle=NULL;

// Pin assigned to Switch1 closed shutter Digital pin
int SwitchDownPin =5;
bool Value1;
//Value1=false;


// Pin assigned to Switch2 opened shutter Digital pin
int SwitchUpPin= 17;
bool Value2;
//Value2=false;
// Pin assigned to Voltage sensor

// //////////////  RELAY Declaration

// Relay1 Motor on/off SPST
int Rele1Pin=19;

// Relay2 Turn Right/Left DPDT
int Rele2Pin=4


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
		
		// Leer la configuracion que hay en el archivo de configuracion config.json
	Serial.println("Leyendo fichero de configuracion");
	LeeConfig();
	
	// MQTT
	ClienteMQTT = AsyncMqttClient();

	// Dar valor a las strings con los nombres de la estructura de los topics
	MiConfigMqtt.cmndTopic = "cmnd/" + String(MiConfigMqtt.mqtt_topic) + "/#";
	MiConfigMqtt.statTopic = "stat/" + String(MiConfigMqtt.mqtt_topic);
	MiConfigMqtt.teleTopic = "tele/" + String(MiConfigMqtt.mqtt_topic);
	MiConfigMqtt.lwtTopic = MiConfigMqtt.teleTopic + "/LWT";

	// Las funciones callback de la libreria MQTT	
	ClienteMQTT.onConnect(onMqttConnect);
  ClienteMQTT.onDisconnect(onMqttDisconnect);
  ClienteMQTT.onSubscribe(onMqttSubscribe);
  ClienteMQTT.onUnsubscribe(onMqttUnsubscribe);
  ClienteMQTT.onMessage(onMqttMessage);
  ClienteMQTT.onPublish(onMqttPublish);
  ClienteMQTT.setServer(MiConfigMqtt.mqtt_server, 1883);
	ClienteMQTT.setCleanSession(true);
	ClienteMQTT.setClientId("ControlAzimut");
	ClienteMQTT.setCredentials(MiConfigMqtt.mqtt_usuario,MiConfigMqtt.mqtt_password);
	ClienteMQTT.setKeepAlive(4);
	ClienteMQTT.setWill(MiConfigMqtt.lwtTopic.c_str(),2,true,"Offline");

	// WIFI
	WiFi.onEvent(WiFiEventCallBack);
	WiFi.begin();
	
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
	Serial.println("Creando tareas ...");
	

	// Tareas CORE0 gestinadas por FreeRTOS
	xTaskCreatePinnedToCore(TaskGestionRed,"MQTT_Conectar",3000,NULL,1,&THandleTaskGestionRed,0);
	xTaskCreatePinnedToCore(TaskProcesaComandos,"ProcesaComandos",2000,NULL,1,&THandleTaskProcesaComandos,0);
	xTaskCreatePinnedToCore(TaskEnviaRespuestas,"EnviaMQTT",2000,NULL,1,&THandleTaskEnviaRespuestas,0);
	xTaskCreatePinnedToCore(TaskCupulaRun,"CupulaRun",2000,NULL,1,&THandleTaskCupulaRun,0);
	xTaskCreatePinnedToCore(TaskMandaTelemetria,"MandaTelemetria",2000,NULL,1,&THandleTaskMandaTelemetria,0);
	xTaskCreatePinnedToCore(TaskComandosSerieRun,"ComandosSerieRun",1000,NULL,1,&THandleTaskComandosSerieRun,0);
	
	// Tareas CORE1. No lanzo ninguna lo hago en el loop() que es una tarea unica en el CORE1
	// Lo hago asi porque necesito muchisima velocidad y FreeRTOS solo puede cambiar entre tareas a 1Khz.
	
	Serial.println("Sistema Iniciado");

	// Esto estaria guay, pero dispara el Watchdog en la CPU1
	//portDISABLE_INTERRUPTS();

	// Esto creo que no hace nada
	portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
	portENTER_CRITICAL(&mux);
	
/* Código setup del SHUTTER
//////////////////////
/
/////////////////////////
***********************/
	
	Wire.begin();

  
// LED control
pinMode (ledPin, OUTPUT);

// Compass initialization
compass.init();
compass.enableDefault();

// Comenzamos el sensor DHT
//dht.begin();
dht.setup(dhtPin, DHTesp::DHT11); // for DHT11 connect DHT sensor to GPIO 14
//dht.setup(DHTpin, DHTesp::DHT22); //for DHT22 Connect DHT sensor to GPIO 14
  
  /*
  Calibration values; the default values of +/-32767 for each axis
  lead to an assumed magnetometer bias of 0. Use the Calibrate example
  program to determine appropriate values for your particular unit.
  */
compass.m_min = (LSM303::vector<int16_t>){-32767, -32767, -32767};
compass.m_max = (LSM303::vector<int16_t>){+32767, +32767, +32767};

// make the pushbutton's pin an input:
pinMode(SwitchDownPin, INPUT);
pinMode(SwitchUpPin, INPUT);

// make the pin 19 an output to activate Relay1:
 
pinMode(Rele1Pin,OUTPUT);

/***************** FIN de set up del shutter
///////////////////////
*/


}

#pragma endregion

#pragma region Compuerta
//  Codigo de la compuerta en el loop
void Compuerta() {
	
	
compass.read();

  
  
  /*
  When given no arguments, the heading() function returns the angular
  difference in the horizontal plane between a default vector and
  north, in degrees.
  
  The default vector is chosen by the library to point along the
  surface of the PCB, in the direction of the top of the text on the
  silkscreen. This is the +X axis on the Pololu LSM303D carrier and
  the -Y axis on the Pololu LSM303DLHC, LSM303DLM, and LSM303DLH
  carriers.
  
  To use a different vector as a reference, use the version of heading()
  that takes a vector argument; for example, use
  
    compass.heading((LSM303::vector<int>){0, 0, 1});
  
  to use the +Z axis as a reference.
  */
float heading = compass.heading();
  // Compass heading display
  
Serial.print("Rumbo: ");
Serial.print(heading);
Serial.print("\t");
 
// Leemos la humedad relativa del DHT11
// Reading temperature and humidity takes about 250 milliseconds!
// Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
TempAndHumidity lastValues = dht.getTempAndHumidity();

Serial.print("Temperature: " + String(lastValues.temperature,0));
Serial.print("\t");
Serial.print("Humidity: " + String(lastValues.humidity,0));
Serial.print("\t");
 
delay(1000);
  
 // Show if switch is connected or not. Shutter closed or opened. If those values are 0, the shutter is in anywhere between.
Value1=digitalRead(SwitchDownPin);
Value2=digitalRead(SwitchUpPin);
// Test of relay1. If switchDown start. Control of these relays comes from MQTT

digitalWrite(Rele1Pin,Value1); // Set relay on, if switch is on. /// In final SW send another parameter from Visual C# to turn on this relay
digitalWrite(ledPin,Value1);
  // print out the state of the button:
  

 


Serial.print("Compuerta cerrada: ");
Serial.print(Value1); 
Serial.print("\t");
Serial.print("Compuerta abierta: ");
Serial.print(Value2);
Serial.print("\t"); 
Serial.print("Relé 1 On/off ");
Serial.println(Value1);
    
delay(500); 
  
 // Serial.print("Hello from DFRobot ESP-WROOM-32");
//digitalWrite (ledPin, HIGH);  // turn on the LED
 // delay(4000);
 // digitalWrite (ledPin, LOW); // turn off the LED
  //Serial.println("Hola mundo");
//delay(2000); // wait for half a second or 500 milliseconds
  
	
	
	
	
}
#pragma endregion

#pragma region Funcion Loop() de ARDUINO

// Funcion LOOP de Arduino
void loop() {
		
		// Esto si que me suaviza el golpe motor.	
		//portDISABLE_INTERRUPTS();

		// Solo la funcion de la libreria del Stepper que se come un nucleo casi.
		//ControladorStepper.run();


		//portENABLE_INTERRUPTS();
	Compuerta();
}

#pragma endregion


/// FIN DEL PROGRAMA ///