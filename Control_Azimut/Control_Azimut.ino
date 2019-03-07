
/// INICIO DEL PROGRAMA ///


#pragma region COMENTARIOS

// Controlador ASCOM para BMDome1
//
// Description:	Driver para controlador de Domo desarrollado por Bilbaomakers
//				para el proyecto de observatorio astronomico en Marcilla de Campos.
//				Control de azimut de la cupula y apertura cierre del shutter.
//				Hardware de control con Arduino y ESP8266
//				Comunicaciones mediante MQTT
//
// Implements:	ASCOM Dome interface version: 6.4SP1
// Author:		Diego Maroto - BilbaoMakers 2019 - info@bilbaomakers.org



// Cosas a hacer en el futuro
/*

		- Implementar uno a varios Led WS2812 para informar del estado de lo importante
				
*/

// Cosas de configuracion a pasar desde el Driver

/*

		- El Azimut cuando esta en HOME
		- El Azimut del PARK


*/


#pragma endregion


#pragma region Includes de librerias usadas por el proyecto


#include <SerialCommands.h>				// Libreria para la gestion de comandos por el puerto serie https://github.com/ppedro74/Arduino-SerialCommands
#include <MQTTClient.h>					// Libreria MQTT: https://github.com/256dpi/arduino-mqtt
#include <AccelStepper.h>				// Para controlar el stepper como se merece: https://www.airspayce.com/mikem/arduino/AccelStepper/classAccelStepper.html
#include <FS.h>							// Libreria Sistema de Ficheros
#include <ESP8266WiFi.h>			    // Para las comunicaciones WIFI
#include <DNSServer.h>					// La necesita WifiManager para el portal captivo
#include <ESP8266WebServer.h>			// La necesita WifiManager para el formulario de configuracion
#include <WiFiManager.h>				// Para la gestion avanzada de la wifi
#include <ArduinoJson.h>				// OJO: Tener instalada una version NO BETA (a dia de hoy la estable es la 5.13.4). Alguna pata han metido en la 6
#include <string>						// Para el manejo de cadenas
#include <Bounce2.h>					// Libreria para filtrar rebotes de los Switches: https://github.com/thomasfredericks/Bounce2

#pragma endregion


#pragma region Constantes y configuracion. Modificable aqui por el usuario

// Para el estado del controlador
//#define ESTADO_CONTROLADOR_ERROR 0
//#define ESTADO_CONTROLADOR_OFFLINE 1
//#define ESTADO_CONTROLADOR_READY 2

// Para la configuracion de conjunto Mecanico de arrastre
static const uint8_t MECANICA_STEPPER_PULSEPIN = D2;				// Pin de pulsos del stepper
static const uint8_t MECANICA_STEPPER_DIRPIN = D1;					// Pin de direccion del stepper
static const uint8_t MECANICA_STEPPER_ENABLEPING = D0;				// Pin de enable del stepper
static const boolean MECANICA_STEPPER_INVERTPINS = true;			// Invertir la logica de los pines de control (pulso 1 o pulso 0)
static const float MECANICA_STEPPER_MAXSPEED = 1000;				// Velocidad maxima del stepper (pasos por segundo)
static const float MECANICA_STEPPER_MAXACELERAION = 300;			// Aceleracion maxima del stepper
static const short MECANICA_PASOS_POR_VUELTA_MOTOR = 400;			// Numero de pasos por vuelta del STEPPER
static const short MECANICA_RATIO_REDUCTORA = 6;					// Ratio de reduccion de la reductora
static const short MECANICA_DIENTES_PINON_ATAQUE = 15;				// Numero de dientes del piños de ataque
static const short MECANICA_DIENTES_CREMALLERA_CUPULA = 880;		// Numero de dientes de la cremallera de la cupula

// Otros sensores
static const uint8_t MECANICA_SENSOR_HOME = D5;						// Pin para el sensor de HOME

// Valores para los Tickers
unsigned long TIEMPO_TICKER_LENTO = 10000;
unsigned long TIEMPO_TICKER_RAPIDO = 500;

# pragma endregion


#pragma region Variables y estructuras


// Estructura para la configuracion del Stepper de Azimut
struct MECANICACFG
{

	// Variables para el Stepper
	uint8_t StepperPulse = MECANICA_STEPPER_PULSEPIN;
	uint8_t StepperDir = MECANICA_STEPPER_DIRPIN;
	uint8_t StepperEnable = MECANICA_STEPPER_ENABLEPING;
	float StepperMaxSpeed = MECANICA_STEPPER_MAXSPEED;
	float StepperAceleration = MECANICA_STEPPER_MAXACELERAION;


} MiConfigStepper;


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
	// Y estos como son para publicar, defino la raiz de la jerarquia. Luego cuando publique ya añado al topic lo que necesite (por ejemplo tele/AZIMUT/LWT , etc ...)
	String statTopic;
	String teleTopic;

} MiConfigMqtt;


// flag para saber si tenemos que salvar los datos en el fichero de configuracion.
bool shouldSaveConfig = false;
//bool shouldSaveConfig = true;

// Para los timers
// unsigned long millis1;

#pragma endregion


#pragma region Objetos


// Wifimanager (aqui para que se vea tambien en el Loop)
WiFiManager wifiManager;

// Para la conexion MQTT
WiFiClient Clientered;
MQTTClient ClienteMQTT;

long lastMsg = 0;
char msg[50];
int value = 0;

// Controlador Stepper
AccelStepper ControladorStepper;

// Objetos debouncer para los switches
Bounce Debouncer_HomeSwitch = Bounce();


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
	long TargetAzimut;						// Variable interna para el destino del movimiento
	
	// Funciones Callback. Son funciones "especiales" que yo puedo definir FUERA de la clase y disparar DENTRO (GUAY).
	// Por ejemplo "una funcion que envie las respuestas a los comandos". Aqui no tengo por que decir ni donde ni como va a enviar esas respuestas.
	// Solo tengo que definirla y cuando cree el objeto de esta clase en mi programa, creo la funcion con esta misma estructura y se la "paso" a la clase
	// que la usara como se usa cualquier otra funcion y ella sabra que hacer

	typedef void(*RespondeComandoCallback)(String comando, String respuesta);			// Definir como ha de ser la funcion de Callback (que le tengo que pasar y que devuelve)
	RespondeComandoCallback MiRespondeComandos = nullptr;								// Definir el objeto que va a contener la funcion que vendra de fuera AQUI en la clase.

	typedef void(*EnviaTelemetriaCallback)();											// Lo mismo para la funcion de envio de telemetria
	EnviaTelemetriaCallback MiEnviadorDeTelemetria = nullptr;

	long GradosToPasos(long grados);													// Convierte Grados a Pasos segun la mecanica
	long PasosToGrados(long pasos);														// Convierte pasos a grados segun la mecanica
	

public:

	BMDomo1();		// Constructor (es la funcion que devuelve un Objeto de esta clase)
	~BMDomo1() {};	// Destructor (Destruye el objeto, o sea, lo borra de la memoria)


	//  Variables Publicas
	String HardwareInfo;						// Identificador del HardWare y Software
	bool ComOK;									// Si la wifi y la conexion MQTT esta OK
	bool DriverOK;								// Si estamos conectados al driver del PC y esta OK
	bool HardwareOK;							// Si nosotros estamos listos para todo (para informar al driver del PC)
	bool Slewing;								// Si alguna parte del Domo esta moviendose
	bool AtHome;								// Si esta parada en HOME
	unsigned long TickerLento;					// Ticker lento (envio de JSON Info, Reconexiones, etc .....)
	unsigned long TickerRapido;					// Otro Ticker para cosas mas rapidas
												

	// Funciones Publicas

	String MiEstadoJson();											// Devuelve un JSON con los estados en un array de 100 chars (la libreria MQTT no puede con mas de 100)
	void MoveTo(int azimut);										// Mover la cupula a un azimut
	void IniciaCupula(String parametros);							// Inicializar la cupula
	void ApagaCupula(String parametros);							// Apagar la cupula normalmente.
	void FindHome();												// Mueve la cupula a Home
	long GetCurrentAzimut();									    // Devuelve el azimut actual de la cupula
	void Run();														// Actualiza las propiedades de estado de este objeto en funcion del estado de motores y sensores
	void SetRespondeComandoCallback(RespondeComandoCallback ref);	// Definir la funcion para pasarnos la funcion de callback del enviamensajes
	void SetEnviaTelemetriaCallback(EnviaTelemetriaCallback ref);   // Definir la funcion para pasarnos la funcion de callback del enviaTelemetria

};

#pragma endregion


#pragma region IMPLEMENTACIONES BMDomo1

// Constructor. Lo que sea que haya que hacer antes de devolver el objeto de esta clase al creador.
BMDomo1::BMDomo1() {	

	HardwareInfo = "BMDome1.HWAz1.0";
	Inicializando = false;
	ComOK = false;
	DriverOK = false;
	HardwareOK = false;
	Slewing = false;			
	BuscandoCasa = false;
	AtHome = false;
	TickerLento = millis();
	TickerRapido = millis();
	
}

#pragma region Funciones Privadas

// Traduce Grados a Pasos segun la mecanica
long BMDomo1::GradosToPasos(long grados) {

	long Pulsos = (MECANICA_DIENTES_CREMALLERA_CUPULA * MECANICA_RATIO_REDUCTORA * MECANICA_PASOS_POR_VUELTA_MOTOR * grados) / (MECANICA_DIENTES_PINON_ATAQUE * 360);
	return Pulsos;

}

// Traduce Pasos a Grados segun la mecanica
long BMDomo1::PasosToGrados(long pasos) {

	long Grados = (pasos * MECANICA_DIENTES_PINON_ATAQUE * 360) / (MECANICA_DIENTES_CREMALLERA_CUPULA * MECANICA_RATIO_REDUCTORA * MECANICA_PASOS_POR_VUELTA_MOTOR);
	return Grados;

}


#pragma endregion


#pragma region Funciones Publicas


// Pasar a esta clase la funcion callback de fuera. Me la pasan desde el programa con el metodo SetRespondeComandoCallback
void BMDomo1::SetRespondeComandoCallback(RespondeComandoCallback ref) {

	MiRespondeComandos = (RespondeComandoCallback)ref;

}

// Pasar a esta clase la funcion callback de fuera. Me la pasan desde el programa con el metodo SetEnviaTelemetriaCallback
void BMDomo1::SetEnviaTelemetriaCallback(EnviaTelemetriaCallback ref) {

	MiEnviadorDeTelemetria = (EnviaTelemetriaCallback)ref;

}

// Metodo que devuelve un JSON con el estado
String BMDomo1::MiEstadoJson() {

	// Esto crea un objeto de tipo JsonObject para el "contenedor de objetos a serializar". De tamaño Objetos + 1
	const int capacity = JSON_OBJECT_SIZE(7);
	StaticJsonBuffer<capacity> jBuffer;

	//DynamicJsonBuffer jBuffer;

	JsonObject& jObj = jBuffer.createObject();

	// Esto llena de objetos de tipo "pareja propiedad valor"
	jObj.set("HWAInf", HardwareInfo);						// Info del Hardware
	jObj.set("COMSta", ComOK);								// Info de la conexion WIFI y MQTT
	jObj.set("DRVSta", DriverOK);							// Info de la comunicacion con el DRIVER ASCOM (o quiza la cambiemos para comunicacion con "cualquier" driver, incluido uno nuestro
	jObj.set("HWASta", HardwareOK);							// Info del estado de inicializacion de la mecanica
	jObj.set("Az", GetCurrentAzimut());						// Posicion Actual (de momento en STEPS)
	jObj.set("Steps", ControladorStepper.currentPosition());  // Posicion en pasos del objeto del Stepper

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

	return PasosToGrados(ControladorStepper.currentPosition());
	
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


			ControladorStepper.moveTo(GradosToPasos(grados));
			
			MiRespondeComandos("GOTO", "OK_MOVE_TO " + String(grados));



		}

		else {
			
			MiRespondeComandos("GOTO", "OUT_OF_RANGE");

		}
		

	}
	

}

// Esta funcion se lanza desde el loop. Es la que hace las comprobaciones. No debe atrancarse nunca tampoco (ni esta ni ninguna)
void BMDomo1::Run() {


	// Comprobaciones de iteracion con el Hardware. Como esto es MUY importante se comprueba siempre que es ejecuta el RUN

	// Una importante es actualizar la propiedad Slewing con el estado del motor paso a paso. 
	Slewing = ControladorStepper.isRunning();

	// Leer el Switch de HOME y hacer cosas en funcion de otras cosas.
	// Si esta PULSADO (LOW).
	if (!Debouncer_HomeSwitch.read()) {

		// Actualizar la propiedad ATHome
		AtHome = true;
		
		// Pero es que ademas si esta pulsado, nos estamos moviendo y estamos "buscando casa" .....
		if (BuscandoCasa && Slewing) {

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

		
	// Otra cosa a comprobar es si estamos buscando home y el motor se ha parado sin llegar a hacer lo de arriba
	// Un supuesto raro, pero eso es que no hemos conseguido detectar el switch home
	if (!Slewing && BuscandoCasa) {

		MiRespondeComandos("FINDHOME", "FAILED_HOME_SW_ERROR");
		BuscandoCasa = false;
		Inicializando = false;
				
	}

	   
	// Aqui las cosas que hay que hacer cada TICKER_RAPIDO (ni tan rapido como las de arriba ni tan lento como las del TICKER_LENTO
	if ((millis() - TickerRapido) >= TIEMPO_TICKER_RAPIDO) {

		// Mientras se mueve tirar a la salida del comando AZIMUTH la posicion actual
		if (Slewing) {

			MiRespondeComandos("AZIMUTH", String(GetCurrentAzimut()));

		}

		//Actualizar el ticker para contar otro ciclo
		TickerRapido = millis();

	}
	

	// Aqui las cosas que tenemos que hacer cada TICKER LENTO
	if ((millis() - TickerLento) >= TIEMPO_TICKER_LENTO ) {

		// Comprobar si estamos conectados a la Wifi y si no reconectar SOLO SI NO NOS ESTAMOS MOVIENDO
		// Porque esto no estoy seguro si bloquea el programa y si nos estamos moviendo atender al movimiento es lo mas importante
		if (WiFi.status() != WL_CONNECTED && !ControladorStepper.isRunning()) {


			Serial.println("HORROR!!: No estamos conectados a la WIFI.");
			//WiFi.begin();

		}

		// Comprobar si estamos conectados a la wifi pero no al MQTT e intentar reconectar SOLO SI NO NOS ESTAMOS MOVIENDO
		// Porque esto no estoy seguro si bloquea el programa y si nos estamos moviendo atender al movimiento es lo mas importante
		else if (WiFi.status() == WL_CONNECTED && !ClienteMQTT.connected() && !ControladorStepper.isRunning()) {


			Serial.println("HORROR!!: No estamos conectados al MQTT.");
			ConectarMQTT();

		}

		// Y si estamos conectados a los dos enviar el JSON de Info (Aqui no importa si nos estamos moviendo porque esta CREO que es Asincrona
		else
		{

			MiEnviadorDeTelemetria();
			
		}
		
		
		//Actualizar el ticker para contar otro ciclo
		TickerLento = millis();
	}

}

#pragma endregion


#pragma endregion


// Objeto de la clase BMDomo1. Es necesario definirlo aqui debajo de la definicion de clase. No puedo en la region de arriba donde tengo los demas
// Eso es porque la clase usa objetos de fuera. Esto es chapuza pero de momento asi no me lio. Despues todo por referencia y la clase esta debajo de los includes o en una libreria a parte. Asi esntonces estaria OK.
BMDomo1 MiCupula;


#pragma endregion


#pragma region Funciones de la aplicacion


#pragma region funciones de gestion de la configuracion

// Funcion Callback disparada por el WifiManager para que sepamos que hay que hay una nueva configuracion que salvar (para los custom parameters).
void saveConfigCallback() {
	Serial.println("Lanzado SaveConfigCallback");
	shouldSaveConfig = true;
}


// Funcion para leer la configuracion desde el fichero de configuracion
void LeeConfig() {


	if (SPIFFS.begin()) {
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


#pragma region Funciones de gestion de las conexiones Wifi y MQTT

// Funcion lanzada cuando entra en modo AP
void APCallback(WiFiManager *wifiManager) {
	Serial.println("Lanzado APCallback");
	Serial.println(WiFi.softAPIP());
	Serial.println(wifiManager->getConfigPortalSSID());
}


// Para conectar al servidor MQTT y suscribirse a los topics
void ConectarMQTT() {

	// Intentar conectar al controlador MQTT.
	if (ClienteMQTT.connect("ControladorAZ", MiConfigMqtt.mqtt_usuario, MiConfigMqtt.mqtt_password, false)) {

		Serial.println("Conectado al MQTT");

		// Suscribirse al topic de Entrada de Comandos
		if (ClienteMQTT.subscribe(MiConfigMqtt.cmndTopic, 2)) {

			// Si suscrito correctamente
			Serial.println("Suscrito al topic " + MiConfigMqtt.cmndTopic);
									

		}
		
		else { Serial.println("Error Suscribiendome al topic " + MiConfigMqtt.cmndTopic); }

			   
		// funcion para manejar los MSG MQTT entrantes
		ClienteMQTT.onMessage(MsgRecibido);

		// Informar por puerto serie del topic LWT
		Serial.println("Topic LWT: " + (MiConfigMqtt.teleTopic + "/LWT"));
		
		// Si llegamos hasta aqui es estado de las comunicaciones con WIFI y MQTT es OK
		MiCupula.ComOK = true;
		Serial.println("Controlador Azimut Iniciado Correctamente");
		
		
		// Publicar un Online en el LWT
		ClienteMQTT.publish(MiConfigMqtt.teleTopic + "/LWT", "Online", true, 2);
				
		// Enviar el JSON de info al topic 
		MandaTelemetria();

		// Enviar a INITHW un STOPPED para informar que estamos SIN inicializar
		MandaRespuesta("INITHW", "STOPPED");

		// Enviar Azimut actual al resultado del comando AZIMUTH
		MandaRespuesta("AZIMUTH", String(MiCupula.GetCurrentAzimut()));
		
	}

	else {

		Serial.println("Error Conectando al MQTT: " + String(ClienteMQTT.lastError()));
		
	}

}


#pragma endregion


#pragma region Funciones de implementacion de los comandos disponibles por MQTT


// Funcion que se ejecuta cuando recibo un mensage MQTT. Decodificar aqui dentro.
void MsgRecibido(String &topic, String &payload) {

	// Nos basamos en que por ejemplo el comando GOTO 270 lo recibimos con un PAYLOAD 270 en el topic cmnd/XXXXXX/XXXXXX/GOTO

	// Sacamos el prefijo del topic, o sea lo que hay delante de la primera /
	int Indice1 = topic.indexOf("/");
	String Prefijo = topic.substring(0, Indice1);
	
	// Sacamos el "COMANDO" del topic, o sea lo que hay detras de la ultima /
	int Indice2 = topic.lastIndexOf("/");
	String Comando = topic.substring(Indice2 + 1);
		
	// Si el prefijo es cmnd se lo mandamos al manejador de comandos
	if (Prefijo == "cmnd") { ManejadorComandos(Comando, payload); }


}


// Maneja un comando con un parametro. De momento salvo necesidad SOLO 1 parametro
String ManejadorComandos(String comando, String parametros) {

	// Y estas son las 3 variables con las que vamos a trabajar


	if (parametros.indexOf(" ") >> 0) {

		Serial.println("Me ha llegado un comando");
		Serial.println("Comando: " + comando);
		Serial.println("Parametro: " + parametros);

		// COMANDO GOTO
		if (comando == "GOTO") {

			// Mover la cupula
			MiCupula.MoveTo(parametros.toInt());

		}

		// COMANDO STATUS
		else if (comando == "STATUS") {

			MandaTelemetria();
			MandaRespuesta("STATUS", "OK");
		}

		// COMANDO STATUS
		else if (comando == "FINDHOME") {

			
			MiCupula.FindHome();


		}

		// COMANDO STATUS
		else if (comando == "INITHW") {


			MiCupula.IniciaCupula(parametros);


		}

		
	}

	else {

		Serial.println("Me ha llegado un comando con demasiados parametros");

	}

}


// Devuelve al topic correspondiente la respuesta a un comando. Esta funcion la uso como CALLBACK para el objeto cupula
void MandaRespuesta(String comando, String respuesta) {
				
	ClienteMQTT.publish(MiConfigMqtt.statTopic + "/" + comando, respuesta, false, 2);
	
}


// envia al topic tele la telemetria en Json
void MandaTelemetria() {
	
	ClienteMQTT.publish(MiConfigMqtt.teleTopic + "/INFO", MiCupula.MiEstadoJson(), false, 2);

}


#pragma endregion


#pragma region Funciones de implementacion de los comandos disponibles por el puerto serie

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

	ClienteMQTT.setHost(parametro);
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

	ManejadorComandos("STATUS", "NADA");

}


// Manejardor para comandos desconocidos
void cmd_error(SerialCommands* sender, const char* cmd)
{
	sender->GetSerial()->print("ERROR: No se reconoce el comando [");
	sender->GetSerial()->print(cmd);
	sender->GetSerial()->println("]");
}



#pragma endregion


#pragma region Otras funciones Auxiliares


// Funcion que imprime la informacion de la red por el puerto serie
void ImprimeInfoRed() {

	Serial.println("Configuracion de Red:");
	Serial.print("IP: ");
	Serial.println(WiFi.localIP());
	Serial.print("      ");
	Serial.println(WiFi.subnetMask());
	Serial.print("GW: ");
	Serial.println(WiFi.gatewayIP());

}


#pragma endregion	


#pragma endregion



#pragma region Funcion Setup() y Loop() de ARDUINO

// funcion SETUP de Arduino
void setup() {

	// Instanciar el objeto MiCupula. Se inicia el solo (o deberia) con las propiedades en estado guay
	MiCupula = BMDomo1();
	// Asignar funciones Callback de Micupula
	MiCupula.SetRespondeComandoCallback(MandaRespuesta);
	MiCupula.SetEnviaTelemetriaCallback(MandaTelemetria);

	// Puerto Serie
	Serial.begin(115200);
	Serial.println();
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

	// Formatear el sistema de ficheros SPIFFS (para limipar el ESP)
	// SPIFFS.format();


	// Inicializacion de las GPIO

	pinMode(MECANICA_SENSOR_HOME, INPUT_PULLUP);
	Debouncer_HomeSwitch.attach(MECANICA_SENSOR_HOME);
	Debouncer_HomeSwitch.interval(5);
		


	// Leer la configuracion que hay en el archivo de configuracion config.json
	Serial.println("Leyendo fichero de configuracion");
	LeeConfig();
	   	

#pragma region Configuracion e inicializacion de la WIFI y el MQTT

	// Añadir al wifimanager parametros para el MQTT
	WiFiManagerParameter custom_mqtt_server("server", "mqtt server", MiConfigMqtt.mqtt_server, 40);
	WiFiManagerParameter custom_mqtt_port("port", "mqtt port", MiConfigMqtt.mqtt_port, 5);
	WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", MiConfigMqtt.mqtt_topic, 34);
	WiFiManagerParameter custom_mqtt_usuario("usuario", "mqtt user", MiConfigMqtt.mqtt_usuario, 20);
	WiFiManagerParameter custom_mqtt_password("password", "mqtt password", MiConfigMqtt.mqtt_password, 20);

	// Configurar el WiFiManager

	// Borrar la configuracion SSID y Password guardadas en EEPROM (Teoricamente esto hay que hacer despues de hace autoconect no se si esta bien aqui esto)
	//wifiManager.resetSettings();

	wifiManager.setDebugOutput(false);

	// Definirle la funcion para aviso de que hay que salvar la configuracion
	wifiManager.setSaveConfigCallback(saveConfigCallback);

	// Definirle la funcion que se dispara cuando entra en modo AP
	wifiManager.setAPCallback(APCallback);


	// Lo hago directamente en el objeto WIFI porque a traves del Wifimanager no puedo darle el DNS para que me resuelva cositas.
	// WiFi.config(_ip, _gw, _sn, _dns, _dns);
	// Y asi pilla por DHCP
	WiFi.begin();
    
	// Añadir mis parametros custom

	wifiManager.addParameter(&custom_mqtt_server);
	wifiManager.addParameter(&custom_mqtt_port);
	wifiManager.addParameter(&custom_mqtt_topic);
	//wifiManager.addParameter(&custom_mqtt_usuario);
	//wifiManager.addParameter(&custom_mqtt_password);


	// Definir Calidad de Señal Minima para mantenerse conectado.
	// Por defecto sin parametros 8%
	wifiManager.setMinimumSignalQuality();

	// Definir la salida de Debug por el serial del WifiManager
	wifiManager.setDebugOutput(false);

	// Arrancar yo la wifi a mano si quiero ponerle yo aqui una wifi. Si no el wifiManager va a pillar la configuracion de la EEPROM
	//Serial.println(WiFi.begin("MIWIFI", "MIPASSWORD"));
	
	// Timeout para que si no configuramos el portal AP se cierre
	wifiManager.setTimeout(300);

	
	// Gestion de la conexion a la Wifi, Si NO estamos conectados por puerto serie lanza el portatl del WifiManager
	Serial.println("Intentando Conectar a la Wifi .....");

	if (Serial) {

		if (!WiFi.begin() == WL_CONNECTED) {

			Serial.println("Conexion Wifi OK. SSID: " + String(WiFi.SSID()));

		}

		else {

			Serial.println("Conexion Wifi Fallida. Reconfigurala mediante comando WIFI");

		}

	}
		
	else {

		if (wifiManager.autoConnect("BMDomo1", "BMDomo1")) {

			Serial.println("Conexion Wifi OK. SSID: " + String(WiFi.SSID()));
			
		}

		else {

			Serial.println("Conexion Wifi Fallida.");
			
		}

	}
	
	
	// Leer los parametros custom que tiene el wifimanager por si los he actualizado yo en modo AP
	strcpy(MiConfigMqtt.mqtt_server, custom_mqtt_server.getValue());
	strcpy(MiConfigMqtt.mqtt_port, custom_mqtt_port.getValue());
	strcpy(MiConfigMqtt.mqtt_topic, custom_mqtt_topic.getValue());


	// Salvar la configuracion en el fichero de configuracion
	if (shouldSaveConfig) {

		SalvaConfig();
	}

	// Dar valor a las strings con los nombres de la estructura de los topics
	MiConfigMqtt.cmndTopic = "cmnd/" + String(MiConfigMqtt.mqtt_topic) + "/#";
	MiConfigMqtt.statTopic = "stat/" + String(MiConfigMqtt.mqtt_topic);
	MiConfigMqtt.teleTopic = "tele/" + String(MiConfigMqtt.mqtt_topic);
			
	// Construir el cliente MQTT con el objeto cliente de la red wifi y combiar opciones
	ClienteMQTT.begin(MiConfigMqtt.mqtt_server, 1883, Clientered);
	ClienteMQTT.setOptions(5, false, 3000);
	
	// Crear el topic LWT
	ClienteMQTT.setWill((MiConfigMqtt.teleTopic + "/LWT").c_str(), "Offline", true, 2);

	// Esperar un poquito para que se acabe de conectar la wifi bien
	delay(2000);

	// Si la conexion a la wifi es OK ......
	if (WiFi.status() == WL_CONNECTED) {
		
		Serial.println("Estoy conectado a la WIFI. Conectando al Broker MQTT");
		
		ImprimeInfoRed();
		
		ConectarMQTT();
								
	}

#pragma endregion


#pragma region Configuracion Objeto Stepper


	// Instanciar el controlador de Stepper.
	ControladorStepper = AccelStepper(AccelStepper::DRIVER, MiConfigStepper.StepperPulse, MiConfigStepper.StepperDir);
	ControladorStepper.disableOutputs();
	ControladorStepper.setCurrentPosition(0);
	ControladorStepper.setEnablePin(MiConfigStepper.StepperEnable);
	ControladorStepper.setMaxSpeed(MiConfigStepper.StepperMaxSpeed);
	ControladorStepper.setAcceleration(MiConfigStepper.StepperAceleration);
	ControladorStepper.setPinsInverted(MECANICA_STEPPER_INVERTPINS, MECANICA_STEPPER_INVERTPINS, MECANICA_STEPPER_INVERTPINS);
	//ControladorStepper.setMinPulseWidth(30); // Ancho minimo de pulso en microsegundos


#pragma endregion

	
	// Habilitar WatchDog
	wdt_enable(WDTO_500MS);
	

}


// Funcion LOOP de Arduino
void loop() {


	// Loop de los debouncers
	Debouncer_HomeSwitch.update();
	

	// Loop del objeto Cupula
	MiCupula.Run();
		
	
	// Loop del cliente MQTT
	ClienteMQTT.loop();
	   

	// Loop del Controlador del Stepper
	ControladorStepper.run(); // Esto hay que llamar para que "run ...."


	// Loop de leer comandos por el puerto serie
	serial_commands_.ReadSerial();

		
	// Resetear contador de WatchDog
	wdt_reset();

}

#pragma endregion


/// FIN DEL PROGRAMA ///