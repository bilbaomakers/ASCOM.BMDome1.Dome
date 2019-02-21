
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

#pragma endregion


#pragma region Constantes y configuracion

// Para el estado del controlador
#define ESTADO_CONTROLADOR_ERROR 0
#define ESTADO_CONTROLADOR_OFFLINE 1
#define ESTADO_CONTROLADOR_READY 2


// Para la configuracion de conjunto Mecanico de arrastre
#define MECANICA_PASOS_POR_VUELTA_MOTOR = 200		// Numero de pasos por vuelta del STEPPER
#define MECANICA_MICROPASOS_CONTROLADORA = 64		// Numero de micropasos en la controladora
#define MECANICA_RATIO_REDUCTORA = 6				// Ratio de reduccion de la reductora
#define MECANICA_DIENTES_PINON_ATAQUE = 15			// Numero de dientes del piños de ataque
#define MECANICA_DIENTES_CREMALLERA_CUPULA = 500	// Numero de dientes de la cremallera de la cupula

# pragma endregion


#pragma region Variables y estructuras


// Estructura para la configuracion del Stepper de Azimut
struct STEPPERCFG
{

	// Variables para el Stepper
	uint8_t StepperPulse = D2;
	uint8_t StepperDir = D1;
	uint8_t StepperEnable = D0;
	float StepperMaxSpeed = 500;
	float StepperAceleration = 100;

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
unsigned long millis1;

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



#pragma endregion


#pragma region CLASE BMDomo1 - Clase principial para el objeto que representa la cupula, sus estados, propiedades y acciones

// Definicion de la clase y sus objetos. Probablemente vaya creciendo e detrimento de funciones de la aplicacion
class BMDomo1
{


private:



public:

	BMDomo1();		// Constructor
	~BMDomo1() {}; //  Destructor

	//  Variables Publicas
	
	String HardwareInfo;						// Identificador del HardWare y Software
	bool DriverOK;								// Si estamos conectados al driver del PC y esta OK
	bool HardwareOK;							// Si nosotros estamos listos para todo (para informar al driver del PC)
	bool Moviendose;							// Si el motor esta en marcha
	bool BuscandoCasa;							// Buscando Home
	bool Calibrando;							// Calibrando
	bool EnHome;								// Si esta parada en HOME
	bool MecanicaOK;							// Si estamos listos para la operacion (toda la mecanica esta ready e inicializada)
	int curr_azimut;							// Azimut actual de la cupula

	// Funciones Publicas

	bool MueveCupula(int azimut);				// Mover la cupula a un azimut
	bool IniciaCupula();						// Inicializar la cupula
	bool BuscaHome();							// Mueve la cupula a Home

};




// Implementacion de los objetos de la clase

// Constructor
BMDomo1::BMDomo1() {

	HardwareInfo = "BMDome1.HWAz1.0";
	DriverOK = false;
	HardwareOK = false;
	Moviendose = false;
	BuscandoCasa = false;
	Calibrando = false;
	EnHome = false;
	MecanicaOK = false;

}


// FUNCIONES


bool BMDomo1::IniciaCupula() {

	
	// Comprobaciones a hacer antes de inicializar la mecanica
	if (DriverOK && HardwareOK && !Moviendose) {

		// Y cosas a hacer para inicializar la cupula

		// Activar el motor
		ControladorStepper.enableOutputs();
		
		// Busca Home y comprobar que se ha buscado bien
		if (BuscaHome()) {
			
			MecanicaOK = true;

		}
			   
	}


	else
	{

		// Publicar en algun post que no se puede iniciar


	}


}


// Funcion para mover fisicamente la cupula
bool BMDomo1::MueveCupula(int target_azimut) {

	// A hacer para mover la cupula a un azimut determinado
		

}


bool BMDomo1::BuscaHome() {



}


// Objeto de la clase BMDomo1. Es necesario definirlo aqui debajo de la definicion de clase no puedo en la region de arriba donde tengo los demas
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
		
		// Si llegamos hasta aqui es estado del controlador es OK
		MiCupula.HardwareOK = true;
		Serial.println("Controlador Azimut Iniciado Correctamente");
		
		
		// Publicar un Online en el LWT
		ClienteMQTT.publish(MiConfigMqtt.teleTopic + "/LWT", "Online", true, 2);

		// Enviar el JSON de info al topic 
		SendInfo();

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

// Para enviar el JSON de estado al topic INFO. Lanzada por el ticker o a peticion con el comando ESTADO
void SendInfo() {
	
	
	// Crear un Buffer para los objetos a serializar, en este caso y de momento uno estatico para 3 objetos
	const int capacity = JSON_OBJECT_SIZE(3);
	StaticJsonBuffer<capacity> jBuffer;

	// Aqui creamos el objeto "generico" y vacio JsonObject que usaremos desde aqui
	JsonObject& jObj = jBuffer.createObject();

	jObj.set("HWAInf", MiCupula.HardwareInfo);
	jObj.set("HWASta", MiCupula.HardwareOK);

	// Crear un buffer donde almacenar la cadena de texto del JSON
	char JSONmessageBuffer[100];

	// Tirar al buffer la cadena de objetos serializada en JSON
	jObj.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
	

	// Publicarla en el post de INFO
	ClienteMQTT.publish(MiConfigMqtt.teleTopic + "/INFO", JSONmessageBuffer,false,2);
	
	//Serial.println(JSONmessageBuffer);
		
	}


// Maneja un comando con un parametro. De momento salvo necesidad SOLO 1 parametro
String ManejadorComandos(String comando, String parametros) {

	// Y estas son las 3 variables con las que vamos a trabajar


	if (parametros.indexOf(" ") >> 0) {

		Serial.println("Me ha llegado un comando");
		Serial.println("Comando: " + comando);
		Serial.println("Parametro: " + parametros);

		if (comando == "GOTO") {

			// Mover la cupula
			MiCupula.MueveCupula(parametros.toInt());

		}

		else if (comando == "STATUS") {

			SendInfo();
			MandaRespuesta(comando, "JSON enviado al topic de INFO");

		}



	}

	else {

		Serial.println("Me ha llegado un comando con demasiados parametros");

	}

}

// Devuelve al topic correspondiente la respuesta a un comando
boolean MandaRespuesta(String comando, String respuesta) {


	ClienteMQTT.publish(MiConfigMqtt.statTopic + "/" + comando, respuesta, false, 2);


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

	SendInfo();

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
	// BMDomo1 MiCupula = BMDomo1();
	
	
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


	// Instanciar el controlador de Stepper. Se le pasa el pin de pulsos y el de direccion en el constructor y despues el enable si queremos usarlo
	ControladorStepper = AccelStepper(AccelStepper::DRIVER, MiConfigStepper.StepperPulse, MiConfigStepper.StepperDir);
	ControladorStepper.setEnablePin(MiConfigStepper.StepperEnable);
	ControladorStepper.disableOutputs();
	ControladorStepper.setMaxSpeed(MiConfigStepper.StepperMaxSpeed);
	ControladorStepper.setAcceleration(MiConfigStepper.StepperAceleration);
	//ControladorStepper.setMinPulseWidth(30); // Ancho minimo de pulso en microsegundos


#pragma endregion

	
	

	// Habilitar WatchDog
	wdt_enable(WDTO_500MS);
	
	// Temporizador 1
	millis1 = millis();


}


// Funcion LOOP de Arduino
void loop() {
  	
	
	
	// Loop del cliente MQTT
	ClienteMQTT.loop();
	   

	// Funcion para "hacer cosas periodicamente".

	if ((millis() - millis1) >= 10000) {

		// Comprobar si estamos conectados a la Wifi y si no reconectar
		if (WiFi.status() != WL_CONNECTED) {


			Serial.println("HORROR!!: No estamos conectados a la WIFI.");
			//WiFi.begin();

		}
		
		// Comprobar si estamos conectados a la wifi pero no al MQTT e intentar reconectar
		else if (WiFi.status() == WL_CONNECTED && !ClienteMQTT.connected()) {

				
			Serial.println("HORROR!!: No estamos conectados al MQTT.");
			ConectarMQTT();

		}
					   
		// Y si estamos conectados a los dos enviar el JSON de Info
		else
		{

			SendInfo();

		}
		
				
		//Actualizar la variable millis1 para contar otro periodo nuevo
		millis1 = millis();

	}
	

	// Loop del Controlador del Stepper
	ControladorStepper.run(); // Esto hay que llamar para que "run ...."


	// Loop de leer comandos por el puerto serie
	serial_commands_.ReadSerial();

		
	// Resetear contador de WatchDog
	wdt_reset();

}

#pragma endregion


/// FIN DEL PROGRAMA ///