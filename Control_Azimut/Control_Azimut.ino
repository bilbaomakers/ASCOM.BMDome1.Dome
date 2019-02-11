
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
		- Implementar un boton o algo para forzar el portal del WifiManager por si hay que cambiar la config del MQTT
		

*/



#include <SerialCommands.h>				// Libreria para la gestion de comandos por el puerto serie https://github.com/ppedro74/Arduino-SerialCommands
#include <MQTT.h>						// Libreria MQTT (2 includes): https://github.com/256dpi/arduino-mqtt
#include <MQTTClient.h>
#include <AccelStepper.h>				// Para controlar el stepper como se merece: https://www.airspayce.com/mikem/arduino/AccelStepper/classAccelStepper.html
#include <FS.h>							// Libreria Sistema de Ficheros
#include <ESP8266WiFi.h>			    // Para las comunicaciones WIFI
#include <DNSServer.h>					// La necesita WifiManager para el portal captivo
#include <ESP8266WebServer.h>			// La necesita WifiManager para el formulario de configuracion
#include <WiFiManager.h>				// Para la gestion avanzada de la wifi
#include <ArduinoJson.h>				// OJO: Tener instalada una version NO BETA (a dia de hoy la estable es la 5.13.4). Alguna pata han metido en la 6
#include <string>						// Para el manejo de cadenas
#include <Ticker.h>						// Para los "eventos periodicos"	


#pragma region Estructuras y Enumeraciones

// Enum para el estado del controlador
typedef enum EnumEstadoControlador {
	HWOffline,
	HWReady
};

// Estructura para los elementos de la configuracion del Domo
typedef struct DomeConfiguration {
	long int PasosParaGiroCompleto;
	float HomeAzimuth;
	float ParkAzimuth;
	bool IsReversed;
	float Checksum;
} dome_config;




# pragma endregion


#pragma region Variables

// Identificador del HardWare y Software
String HardwareInfo = "BMDome1.HWAz1.0";


// Variables para el Stepper
uint8_t StepperPulse = D2;
uint8_t StepperDir = D1;
uint8_t StepperEnable = D0;
float StepperMaxSpeed = 500;
float StepperAceleration = 100;


//Valores Wifimanager para MQTT. Si existen posterioirmente en el fichero de configuracion en el SPIFFS se sobreescibiran con esos valores.
char mqtt_server[40] = "";
char mqtt_port[6] = "";
char mqtt_topic[33] = "";
char mqtt_usuario[19] = "";
char mqtt_password[19] = "";

// flag para saber si tenemos que salvar los datos en el fichero de configuracion.
bool shouldSaveConfig = false;
//bool shouldSaveConfig = true;


// flags que cambian los tickers (porque no hay que llamar a cosas asincronas como MQTT Publish desde las callback de los tickers, lio de interrupciones y catacarsh)

boolean Tick30s_f = false;

#pragma endregion


#pragma region Objetos

// Objetos Ticker
Ticker Tick30s;				// Para hacer cosas cada 30 segundos


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

// Objeto para el estado del controlador
EnumEstadoControlador EstadoControlador;


#pragma endregion


#pragma region Funciones

// Funcion Callback disparada por el WifiManager para que sepamos que hay que hay una nueva configuracion que salvar (para los custom parameters).
void saveConfigCallback() {
	Serial.println("Lanzado SaveConfigCallback");
	shouldSaveConfig = true;
}

// Funcion lanzada cuando entra en modo AP
void APCallback(WiFiManager *wifiManager) {
	Serial.println("Lanzado APCallback");
	Serial.println(WiFi.softAPIP());
	Serial.println(wifiManager->getConfigPortalSSID());
}

// Para conectar al servidor MQTT y suscribirse a los topics
void ConectarMQTT() {

	// Intentar conectar al controlador MQTT.
	if (ClienteMQTT.connect("ControladorAZ", mqtt_usuario, mqtt_password, false)) {

		Serial.println("Conectado al MQTT");

		// Suscribirse al topic de Entrada de Comandos
		if (ClienteMQTT.subscribe(String(mqtt_topic) + "/CMD", 2)) {

			// Si suscrito correctamente
			Serial.println("Suscrito al topic " + String(mqtt_topic) + "/CMD");
									

		}
		else { Serial.println("Error Suscribiendome al topic " + String(mqtt_topic) + "/CMD"); }

			   
		// funcion para manejar los MSG entrantes
		ClienteMQTT.onMessage(MsgRecibido);

		


		// Si llegamos hasta aqui es estado del controlador es Ready
		EstadoControlador = HWReady;
		Serial.println("Controlador en estado: " + String(EstadoControlador));
		// Enviar topic de Info
		SendInfo();

	}

	else {

		Serial.println("Error Conectando al MQTT: " + String(ClienteMQTT.lastError()));
		//Serial.println("Estado del Connected:" + String(ClienteMQTT.connected()));

	}


}


void CambiaFlagsTicker30s() {

	Tick30s_f = true;

}


// Para enviar el JSON de estado al topic INFO. Serializa un JSON a partir de un OBJETO
void SendInfo() {
	
	
	// Crear un Buffer para los objetos a serializar, en este caso y de momento uno estatico para 3 objetos
	const int capacity = JSON_OBJECT_SIZE(3);
	StaticJsonBuffer<capacity> jBuffer;

	// Aqui creamos el objeto "generico" y vacio JsonObject que usaremos desde aqui
	JsonObject& jObj = jBuffer.createObject();

	jObj.set("HWAInf", HardwareInfo);
	jObj.set("HWASta", EstadoControlador);
	

	// Crear el Array de valores JSON
	//JsonArray& cadena = jObj.createNestedArray("cadena");

	// Crear un buffer donde almacenar la cadena de texto del JSON
	char JSONmessageBuffer[100];

	// Tirar al buffer la cadena de objetos serializada en JSON
	jObj.printTo(JSONmessageBuffer, sizeof(JSONmessageBuffer));
	

	// Publicarla en el post de INFO
	ClienteMQTT.publish(String(mqtt_topic) + "/INFO", JSONmessageBuffer,false,2);
	
	Serial.println(JSONmessageBuffer);
		
	}



// Funcion para movel el Azimut de la cupula
void MueveCupula(int Azimut) {

	ControladorStepper.moveTo(Azimut); // El Moveto Mueve pasos. Yo aqui paso el azimut. Hacer la conversion segun pasos por grado antes

}


// Funcion que se ejecuta cuando recibo un mensage MQTT. Decodificar aqui dentro.
void MsgRecibido(String &topic, String &payload) {

	Serial.println("MSG Recibido en el topic: " + topic);
	Serial.println("Payload: " + payload);

}


// Funcion para leer la configuracion desde el fichero de configuracion
void LeeConfig (){


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
					strcpy(mqtt_server, json["mqtt_server"]);
					strcpy(mqtt_port, json["mqtt_port"]);
					strcpy(mqtt_topic, json["mqtt_topic"]);
					strcpy(mqtt_usuario, json["mqtt_usuario"]);
					strcpy(mqtt_password, json["mqtt_password"]);

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
	json["mqtt_server"] = mqtt_server;
	json["mqtt_port"] = mqtt_port;
	json["mqtt_topic"] = mqtt_topic;
	json["mqtt_usuario"] = mqtt_usuario;
	json["mqtt_password"] = mqtt_password;

	File configFile = SPIFFS.open("/config.json", "w");
	if (!configFile) {
		Serial.println("No se puede abrir el fichero de configuracion");
	}

	//json.prettyPrintTo(Serial);
	json.printTo(configFile);
	configFile.close();
	//end save

}




#pragma endregion


#pragma region COMANDOSSERIAL

// Objetos Comandos
SerialCommand cmd_SSID("SSID", cmd_SSID_hl);
SerialCommand cmd_Password("Password", cmd_Password_hl);
SerialCommand cmd_MQTTSrv("MQTTSrv", cmd_MQTTSrv_hl);
SerialCommand cmd_MQTTUser("MQTTUser", cmd_MQTTUser_hl);
SerialCommand cmd_MQTTPassword("MQTTPassword", cmd_MQTTPassword_hl);
SerialCommand cmd_SaveConfig("SaveConfig", cmd_SaveConfig_hl);


// Manejadores de los comandos. Aqui dentro lo que queramos hacer con cada comando.
void cmd_SSID_hl(SerialCommands* sender)
{
	
	char* parametro = sender->Next();
	if (parametro == NULL)
	{
		sender->GetSerial()->println("ERROR, falta un parametro");
		return;
	}

	sender->GetSerial()->println("SSID: " + String(parametro));
}

void cmd_Password_hl(SerialCommands* sender)
{

	char* parametro = sender->Next();
	if (parametro == NULL)
	{
		sender->GetSerial()->println("ERROR, falta un parametro");
		return;
	}

	sender->GetSerial()->println("Password: " + String(parametro));
}

void cmd_MQTTSrv_hl(SerialCommands* sender)
{

	char* parametro = sender->Next();
	if (parametro == NULL)
	{
		sender->GetSerial()->println("MQTTSrv: " + String(mqtt_server));
		return;
	}

	ClienteMQTT.setHost(parametro);
	strcpy(mqtt_server, parametro);
	
	sender->GetSerial()->println("MQTTSrv: " + String(parametro));
}

void cmd_MQTTUser_hl(SerialCommands* sender)
{

	char* parametro = sender->Next();
	if (parametro == NULL)
	{
		sender->GetSerial()->println("MQTTUser: " + String(mqtt_usuario));
		return;
	}

	strcpy(mqtt_usuario, parametro);
	
	sender->GetSerial()->println("MQTTUser: " + String(parametro));
}

void cmd_MQTTPassword_hl(SerialCommands* sender)
{

	char* parametro = sender->Next();
	if (parametro == NULL)
	{
		sender->GetSerial()->println("MQTTPassword: " + String(mqtt_password));
		return;
	}

	strcpy(mqtt_password, parametro);

	sender->GetSerial()->println("MQTTPassword: " + String(parametro));
}

void cmd_SaveConfig_hl(SerialCommands* sender)
{

	SalvaConfig();

}


// Manejardor para comandos desconocidos
void cmd_error(SerialCommands* sender, const char* cmd)
{
	sender->GetSerial()->print("ERROR: No se reconoce el comando [");
	sender->GetSerial()->print(cmd);
	sender->GetSerial()->println("]");
}

// Buffer para los comandos
char serial_command_buffer_[32];

// Bind del objeto con el puerto serie usando el buffer
SerialCommands serial_commands_(&Serial, serial_command_buffer_, sizeof(serial_command_buffer_), "\r\n", " ");


#pragma endregion




void setup() {

	Serial.begin(115200);
	Serial.println();

	// Añadir los comandos al objeto manejador de comandos serie
	serial_commands_.AddCommand(&cmd_SSID);
	serial_commands_.AddCommand(&cmd_Password);
	serial_commands_.AddCommand(&cmd_MQTTSrv);
	serial_commands_.AddCommand(&cmd_MQTTUser);
	serial_commands_.AddCommand(&cmd_MQTTPassword);
	serial_commands_.AddCommand(&cmd_SaveConfig);
	
	// Manejador para los comandos Serie no reconocidos.
	serial_commands_.SetDefaultHandler(&cmd_error);

	   
	// Para el Estado del controlador
	EstadoControlador = HWOffline;


	// Formatear el sistema de ficheros SPIFFS
	// SPIFFS.format();

	// Leer la configuracion que hay en el archivo de configuracion config.json
	Serial.println("Leyendo fichero de configuracion");
	LeeConfig();
	   	

#pragma region WIFI

	// Añadir al wifimanager parametros para el MQTT
	WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
	WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
	WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 34);
	WiFiManagerParameter custom_mqtt_usuario("usuario", "mqtt user", mqtt_usuario, 20);
	WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 20);

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

	// Funcion autoConnect. Intenta conectar a la WIFI con los parametros de la EEPROM y si no puede o no hay entra en modo AP con SSID CONTROLADORBM y contraseña maker
	// Teoricamente tambien quiero que despues del timeout del AP salga del wifimanager del todo para que siga el programa sin wifi ya lo mostrare por el display.
	Serial.println("Intentando Conectar a la Wifi .....");


	if (!wifiManager.autoConnect("BMDomo1", "BMDomo1")) {

		Serial.println("WIFI Autoconect Fallido");
	}


	else {

		Serial.println("WIFI Autoconnect OK. SSID: " + String (WiFi.SSID()));
	}


	// Leer los parametros custom que tiene el wifimanager por si los he actualizado yo en modo AP
	strcpy(mqtt_server, custom_mqtt_server.getValue());
	strcpy(mqtt_port, custom_mqtt_port.getValue());
	strcpy(mqtt_topic, custom_mqtt_topic.getValue());


	// Salvar la configuracion en el fichero de configuracion
	if (shouldSaveConfig) {

		SalvaConfig();
	}



	Serial.println("Configuracion de Red:");
	Serial.print("IP: ");
	Serial.println(WiFi.localIP());
	Serial.print("      ");
	Serial.println(WiFi.subnetMask());
	Serial.print("GW: ");
	Serial.println(WiFi.gatewayIP());
	
	// Construir el cliente MQTT con el objeto cliente de la red wifi y combiar opciones
	ClienteMQTT.begin(mqtt_server, 1883, Clientered);
	ClienteMQTT.setOptions(5, true, 3000);

	// Esperar un poquito para que se acabe de conectar la wifi bien
	delay(500);

	// Si la conexion a la wifi es OK ......
	if (WiFi.status() == WL_CONNECTED) {
		
		Serial.println("Estoy conectado a la WIFI. Conectando al Broker MQTT");
		
		ConectarMQTT();
						
		
	}

#pragma endregion


#pragma region Stepper


	// Instanciar el controlador de Stepper. Se le pasa el pin de pulsos y el de direccion en el constructor y despues el enable si queremos usarlo
	ControladorStepper = AccelStepper(AccelStepper::DRIVER, StepperPulse, D1);
	ControladorStepper.setEnablePin(StepperEnable);
	ControladorStepper.disableOutputs();
	ControladorStepper.setMaxSpeed(StepperMaxSpeed);
	ControladorStepper.setAcceleration(StepperAceleration);
	//ControladorStepper.setMinPulseWidth(30); // Ancho minimo de pulso en microsegundos
	
#pragma endregion


	// Arrancar Tickers de 30s
	Tick30s.attach(30, CambiaFlagsTicker30s);
	
	Serial.println("Terminado Setup. Iniciando Loop");

	// Habilitar WatchDog
	 wdt_enable(WDTO_500MS);
	 
}



void loop() {
  
	
	// Loop del cliente MQTT
	ClienteMQTT.loop();

	
	if (Tick30s_f) {

		if (!ClienteMQTT.connected()) {

			Serial.println("HORROR!!: No estamos conectados al MQTT.");
			ConectarMQTT();

		}

		else
		{

			SendInfo();

		}
		
				
		Tick30s_f = false;

	}
	
	// Loop del Controlador del Stepper
	ControladorStepper.run(); // Esto hay que llamar para que "run ...."

	// Loop de leer comandos por el puerto serie
	serial_commands_.ReadSerial();

		
	// Resetear contador de WatchDog
	wdt_reset();

}





