
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

#include <MQTTClient.h>
//#include <MQTT.h>
#include <AccelStepper.h>				// Para controlar el stepper como se merece: https://www.airspayce.com/mikem/arduino/AccelStepper/classAccelStepper.html
#include <FS.h>							// Libreria Sistema de Ficheros
#include <ESP8266WiFi.h>          
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          
#include <ArduinoJson.h> 
#include <string>


// Variables para el Stepper
uint8_t StepperPulse = D2;
uint8_t StepperDir = D1;
uint8_t StepperEnable = D0;
float StepperMaxSpeed = 500;
float StepperAceleration = 100;


// Definir Valores. Si existen posterioirmente en el fichero de configuracion en el SPIFFS se sobreescibiran con esos valores.
char mqtt_server[40] = "";
char mqtt_port[6] = "";
char mqtt_topic[33] = "";
char mqtt_usuario[19] = "";
char mqtt_password[19] = "";


// flag para saber si tenemos que salvar los datos
//bool shouldSaveConfig = false;
bool shouldSaveConfig = true;


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


void setup() {

	Serial.begin(115200);
	Serial.println();

	// Formatear el sistema de ficheros SPIFFS
	// SPIFFS.format();

	// Leer la configuracion que hay en el archivo de configuracion config.json
	Serial.println("Montando el sistema de ficheros");

	if (SPIFFS.begin()) {
		Serial.println("Sistema de ficheros montado");
		if (SPIFFS.exists("/config.json")) {
			// Si existe el fichero abrir y leer la configuracion y asignarsela a las variables definidas arriba
			Serial.println("Leyendo configuracion del fichero");
			File configFile = SPIFFS.open("/config.json", "r");
			if (configFile) {
				Serial.println("opened config file");
				size_t size = configFile.size();
				// Declarar un buffer para almacenar el contenido del fichero
				std::unique_ptr<char[]> buf(new char[size]);
				// Leer el fichero al buffer
				configFile.readBytes(buf.get(), size);
				DynamicJsonBuffer jsonBuffer;
				JsonObject& json = jsonBuffer.parseObject(buf.get());
				json.printTo(Serial);
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



	// Añadir al wifimanager parametros para el MQTT
	WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
	WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
	WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 34);
	WiFiManagerParameter custom_mqtt_usuario("usuario", "mqtt user", mqtt_usuario, 20);
	WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 20);

	// Configurar el WiFiManager

	// Borrar la configuracion SSID y Password guardadas en EEPROM (Teoricamente esto hay que hacer despues de hace autoconect no se si esta bien aqui esto)
	//wifiManager.resetSettings();

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

	Serial.println("El Wifi Begin ....");

	// Arrancar yo la wifi a mano si quiero ponerle yo aqui una wifi. Si no el wifiManager va a pillar la configuracion de la EEPROM
	//Serial.println(WiFi.begin("MIWIFI", "MIPASSWORD"));
	Serial.println(WiFi.begin());

	// Timeout para que si no configuramos el portal AP se cierre
	wifiManager.setTimeout(300);

	// Funcion autoConnect. Intenta conectar a la WIFI con los parametros de la EEPROM y si no puede o no hay entra en modo AP con SSID CONTROLADORBM y contraseña maker
	// Teoricamente tambien quiero que despues del timeout del AP salga del wifimanager del todo para que siga el programa sin wifi ya lo mostrare por el display.
	
	Serial.println("Intentando Conectar a la Wifi .....");

	if (!wifiManager.autoConnect("BMDomo1", "BMDomo1")) {

		Serial.println("Salida 1 del Autoconnect");
	}

	else {

		Serial.println("Salida 2 del Autoconnect");
	}

	// Leer los parametros custom que tiene el wifimanager por si los he actualizado yo en modo AP
	strcpy(mqtt_server, custom_mqtt_server.getValue());
	strcpy(mqtt_port, custom_mqtt_port.getValue());
	strcpy(mqtt_topic, custom_mqtt_topic.getValue());


	// Salvar los parametros custom 
	if (shouldSaveConfig) {
		Serial.println("Salvando la configuracion");
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

		json.prettyPrintTo(Serial);
		json.printTo(configFile);
		configFile.close();
		//end save
	}

	Serial.println("local ip");
	Serial.println(WiFi.localIP());
	Serial.println(WiFi.gatewayIP());
	Serial.println(WiFi.subnetMask());
	Serial.println(WiFi.SSID());


	// Cosas a hacer si la conexion es OK
	if (WiFi.status() == WL_CONNECTED) {

		Serial.println("Estoy conectado a la WIFI");
		
		// Construir el cliente MQTT con el objeto cliente de la red wifi
		ClienteMQTT.begin("dmarofer.mooo.info", 1883, Clientered);

		Serial.println("Intentando conectar al MQTT");

		Serial.println(String(mqtt_usuario) + " " + String(mqtt_password));
		
		// Intentar conectar al controlador MQTT.
		
		if (ClienteMQTT.connect("ControladorAZ", mqtt_usuario, mqtt_password, false)) {

			Serial.println("Conectado al MQTT");
		}
		

		else {

			Serial.println("Error Conectando al MQTT: " + String(ClienteMQTT.lastError()) + "Lanzando Portal de Configuracion");
			// Esto aqui no vale porque despues de cambiar la config aqui no haria lo de arriba del setup.
			// wifiManager.startConfigPortal("BMDomo1", "BMDomo1");
											
		}
				
					
		// Si hemos conseguido conectar
		// funcion para manejar los MSG entrantes
		ClienteMQTT.onMessage(MsgRecibido);
		
		// Topics a los que Suscribirse
		if (ClienteMQTT.subscribe(mqtt_topic, 2)) {
		
			Serial.println("Suscrito al topic " + String(mqtt_topic));
			ClienteMQTT.publish(mqtt_topic, "Hola soy el Hardware y estoy Vivo");
		
		}
		else { Serial.println("Error Suscribiendome al topic " + String(mqtt_topic)); }
	}

	


	// Instanciar el controlador de Stepper. Se le pasa el pin de pulsos y el de direccion en el constructor y despues el enable si queremos usarlo
	ControladorStepper = AccelStepper(AccelStepper::DRIVER, StepperPulse, D1);
	ControladorStepper.setEnablePin(StepperEnable);
	ControladorStepper.disableOutputs();
	ControladorStepper.setMaxSpeed(StepperMaxSpeed);
	ControladorStepper.setAcceleration(StepperAceleration);
	//ControladorStepper.setMinPulseWidth(30); // Ancho minimo de pulso en microsegundos
	
	

	Serial.println("Terminado Setup. Iniciando Loop");

	// Habilitar WatchDog
	wdt_enable(WDTO_500MS);


}



void loop() {
  
	ControladorStepper.run(); // Esto hay que llamar para que "run ...."
	


	// Resetear contador de WatchDog
	wdt_reset();

}





void MueveCupula(int Azimut) {

	ControladorStepper.moveTo(Azimut); // El Moveto Mueve pasos. Yo aqui paso el azimut. Hacer la conversion segun pasos por grado antes
		
}


// Funcion que se ejecuta cuando recibo un mensage MQTT. Decodificar aqui dentro.
void MsgRecibido(String &topic, String &payload) {

	Serial.println("MSG Recibido" + topic + " - " + payload);

}