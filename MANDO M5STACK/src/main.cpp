#pragma region COMENTARIOS

/*

ESP32MQTTBASE 1.1
Programa para utilizar de base en tus proyectos MQTT con ESP32
Desarrollado con Visual Code + PlatformIO + Plataforma Espressif 32 Arduino
Implementa las comunicaciones WIFI y MQTT asi como la configuracion de las mismas via comandos
Implementa el envio de comandos via puerto serie o MQTT
Implementa el uso de tareas para multiproceso y para usar ambos cores
Implementa un timer con soporte para FPU (unidad de coma flotante) para utilizar en tareas que requieran mayor velocidad que una Task
Incluye la clase MiProyecto para desarrollar nuestro proyecto.

Author: Diego Maroto - BilbaoMakers 2019 - info@bilbaomakers.org - dmarofer@diegomaroto.net

https://github.com/bilbaomakers/ESP32MQTTBASE

https://bilbaomakers.org/

Licencia: GNU General Public License v3.0 ( mas info en GitHub )

*/

#pragma endregion

#pragma region INCLUDES

#include <M5Stack.h>					// Para el Hardware M5Stack
// Includes de la libreria grafica cfGUI: https://github.com/JF002/cfGUI
#include <Screen.h>
#include <ButtonInfoBar.h>
#include <StatusBar.h>
#include <Button.h>
#include <WidgetMosaic.h>
#include <time.h>
#include <sys/time.h>
#include <AppScreen.h>
#include <UpDownButton.h>

#include <AsyncMqttClient.h>			// Vamos a probar esta que es Asincrona: https://github.com/marvinroger/async-mqtt-client
#include <FS.h>							// Libreria Sistema de Ficheros
#include <WiFi.h>						// Para las comunicaciones WIFI del ESP32
#include <DNSServer.h>					// La necesita WifiManager para el portal captivo
#include <WebServer.h>					// La necesita WifiManager para el formulario de configuracion (ESP32)
#include <ArduinoJson.h>				// OJO: Tener instalada una version NO BETA (a dia de hoy la estable es la 5.13.4). Alguna pata han metido en la 6
#include <string>						// Para el manejo de cadenas
//#include <Bounce2.h>					// Libreria para filtrar rebotes de los Switches: https://github.com/thomasfredericks/Bounce2
#include <SPIFFS.h>						// Libreria para sistema de ficheros SPIFFS
#include <NTPClient.h>					// Para la gestion de la hora por NTP
#include <WiFiUdp.h>					// Para la conexion UDP con los servidores de hora.

using namespace Codingfield::UI;
AppScreen* screen;
StatusBar* topBar;
ButtonInfoBar* bottomBar;
Codingfield::UI::Button* BotonMQTT;
Codingfield::UI::Button* BotonRSSI;
Codingfield::UI::UpDownButton* BotonAZ;
Codingfield::UI::Button* BotonInitHW;
Codingfield::UI::Button* BotonSTOP;
Codingfield::UI::Button* BotonPARK;
WidgetMosaic* mosaic;
Widget* focus;

int32_t editButtonValue = 0;
int32_t editOldButtonValue = 0;

#pragma endregion

#pragma region Constantes y configuracion. Modificable aqui por el usuario

// Para el periodo de repeticon de la tarea gestionada por el timer (en microsegundos);
static const uint64_t TIMER_TICK_US = 200;

// Para el nombre del fichero de configuracion de comunicaciones
static const String FICHERO_CONFIG_COM = "/MiProyectoCom.json";

// Para el nombre del fichero de configuracion del proyecto
static const String FICHERO_CONFIG_PRJ = "/MiProyectoCfg.json";

// Para la zona horaria (horas de diferencia con UTC)
static const int HORA_LOCAL = 2;

#pragma endregion

#pragma region Objetos

// Para la conexion MQTT
AsyncMqttClient ClienteMQTT;

// Los manejadores para las tareas. El resto de las cosas que hace nuestro controlador que son un poco mas flexibles que la de los pulsos del Stepper
TaskHandle_t THandleTaskGUI,THandleTaskMiProyectoRun,THandleTaskProcesaComandos,THandleTaskComandosSerieRun,THandleTaskMandaTelemetria,THandleTaskGestionRed,THandleTaskEnviaRespuestas;	

// Manejadores Colas para comunicaciones inter-tareas
QueueHandle_t ColaComandos,ColaRespuestas;

// Timer Run
//hw_timer_t * timer_stp = NULL;
//portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

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

		// Otras configuraciones permanentes del proyecto
		
		ConfigClass(String fichero);
		~ConfigClass() {};

		boolean leeconfig ();
		boolean escribeconfig ();
		
};

	// Constructor
	ConfigClass::ConfigClass(String fichero){

		c_fichero = fichero;

		mqttserver[0]= '\0';
		mqttport[0] = '\0';
		mqtttopic[0] = '\0';
		mqttusuario[0] = '\0';
		mqttpassword[0] = '\0';

		Wssid[0] = '\0';
		WPasswd[0]  = '\0';	
				
	}

	// Leer la configuracion desde el fichero
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
				Serial.println("Informacion de los comandos con el comando Help");
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
	ConfigClass MiConfig = ConfigClass(FICHERO_CONFIG_COM);

#pragma endregion

#pragma region CLASE MiProyecto - Clase principial para Mi Proyecto

// Una clase tiene 2 partes:
// La primera es la definicion de todas las propiedades y metodos, publicos o privados.
// La segunda es la IMPLEMENTACION de esos metodos o propiedades (que hacen). En C++ mas que nada de los METODOS (que son basicamente funciones)

// Definicion
class MiProyecto {

#pragma region DEFINICIONES MiProyecto
private:

	// Variables Internas para uso de la clase

	bool HayQueSalvar;
	String mificheroconfig;

	// Funciones Callback. Son funciones "especiales" que yo puedo definir FUERA de la clase y disparar DENTRO.
	// Por ejemplo "una funcion que envie las respuestas a los comandos". Aqui no tengo por que decir ni donde ni como va a enviar esas respuestas.
	// Solo tengo que definirla y cuando cree el objeto de esta clase en mi programa, creo la funcion con esta misma estructura y se la "paso" a la clase
	// que la usara como se usa cualquier otra funcion y ella sabra que hacer

	typedef void(*RespondeComandoCallback)(String comando, String respuesta);			// Definir como ha de ser la funcion de Callback (que le tengo que pasar y que devuelve)
	RespondeComandoCallback MiRespondeComandos = nullptr;								// Definir el objeto que va a contener la funcion que vendra de fuera AQUI en la clase.
	

public:

	MiProyecto(String fich_config_miproyecto);						// Constructor (es la funcion que devuelve un Objeto de esta clase)
	~MiProyecto() {};												// Destructor (Destruye el objeto, o sea, lo borra de la memoria)

	//  Variables Publicas
	String HardwareInfo;											// Identificador del HardWare y Software
	bool ComOK;														// Si la wifi y la conexion MQTT esta OK
	
	// Funciones Publicas
	String MiEstadoJson(int categoria);								// Devuelve un JSON con los estados en un array de 100 chars (la libreria MQTT no puede con mas de 100)
	void Run();														// Actualiza las propiedades de estado de este objeto en funcion del estado de motores y sensores
	void SetRespondeComandoCallback(RespondeComandoCallback ref);	// Definir la funcion para pasarnos la funcion de callback del enviamensajes
	boolean LeeConfig();
	boolean SalvaConfig();

};

#pragma endregion


#pragma region IMPLEMENTACIONES MiProyecto

// Constructor. Lo que sea que haya que hacer antes de devolver el objeto de esta clase al creador.
MiProyecto::MiProyecto(String fich_config_miproyecto) {	

	HardwareInfo = "MiProyecto.ESP32.1.0";
	ComOK = false;
	HayQueSalvar = false;
	mificheroconfig = fich_config_miproyecto;

}

#pragma region Funciones Publicas

// Pasar a esta clase la funcion callback de fuera. Me la pasan desde el programa con el metodo SetRespondeComandoCallback
void MiProyecto::SetRespondeComandoCallback(RespondeComandoCallback ref) {

	MiRespondeComandos = (RespondeComandoCallback)ref;

}

// Metodo que devuelve un JSON con el estado
String MiProyecto::MiEstadoJson(int categoria) {

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

boolean MiProyecto::SalvaConfig(){
	

	File mificheroconfig_handler = SPIFFS.open(mificheroconfig, "w");

	if (!mificheroconfig_handler) {
		Serial.println("No se puede abrir el fichero de configuracion de mi proyecto");
		return false;
	}

	if (mificheroconfig_handler.print(MiEstadoJson(1))){

		return true;

	}

	else {

		return false;

	}

}

boolean MiProyecto::LeeConfig(){

	// Sacar del fichero de configuracion, si existe, las configuraciones permanentes
	if (SPIFFS.exists(mificheroconfig)) {

		File mificheroconfig_handler = SPIFFS.open(mificheroconfig, "r");
		if (mificheroconfig_handler) {
			size_t size = mificheroconfig_handler.size();
			// Declarar un buffer para almacenar el contenido del fichero
			std::unique_ptr<char[]> buf(new char[size]);
			// Leer el fichero al buffer
			mificheroconfig_handler.readBytes(buf.get(), size);
			DynamicJsonBuffer jsonBuffer;
			JsonObject& json = jsonBuffer.parseObject(buf.get());
			if (json.success()) {

				Serial.print("Configuracion de mi proyecto Leida: ");
				json.printTo(Serial);
				Serial.println("");
				return true;

			}

			return false;

		}

		return false;

	}

	return false;

}

// Metodos (funciones). TODAS Salvo la RUN() deben ser ASINCRONAS. Jamas se pueden quedar uno esperando. Esperar a lo bobo ESTA PROHIBIDISISISISMO, tenemos MUCHAS cosas que hacer ....

// Esta funcion se lanza desde una Task y hace las "cosas periodicas de la clase". No debe atrancarse nunca tampoco por supuesto (ni esta ni ninguna)
void MiProyecto::Run() {
	
	
	if (HayQueSalvar){

		SalvaConfig();
		HayQueSalvar = false;

	}

}

#pragma endregion


#pragma endregion


// Objeto de la clase MiProyecto.

MiProyecto MiProyectoOBJ(FICHERO_CONFIG_PRJ);

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

	Serial.println("Conexion MQTT: Conectado");
	
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
		MiProyectoOBJ.ComOK = true;
		Serial.print("** ");
		Serial.print(ClienteNTP.getFormattedTime());
		Serial.println(" - SISTEMA INICIADO CORRECTAMENTE **");

	}

}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  
	Serial.println("Conexion MQTT: Desconectado.");

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

// Manda a la cola de respuestas el mensaje de respuesta. Esta funcion la uso como CALLBACK para el objeto MiProyecto
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
			ObjJson.set("RESP",MiProyectoOBJ.MiEstadoJson(1));
			
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

				String COMANDO;
				String PAYLOAD;
				DynamicJsonBuffer jsonBuffer;

				JsonObject& ObjJson = jsonBuffer.parseObject(JSONmessageBuffer);

				if (ObjJson.success()) {
				
					COMANDO = ObjJson["COMANDO"].as<String>();
					PAYLOAD = ObjJson["PAYLOAD"].as<String>();
					
					// De aqui para abajo la retaila de comandos que queramos y lo qude han de hacer.

					// ##### COMANDOS PARA LA GESTION DE LA CONFIGURACION

					if (COMANDO == "WSsid"){
						
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

					else if (COMANDO == "Help"){

						Serial.println("Comandos para la configuracion de las comunicaciones:");
						Serial.println("WSsid <SSID> - Configurar SSID de la Wifi");
						Serial.println("WPasswd <Contraseña> - Configurar contraseña de la Wifi ");
						Serial.println("MQTTSrv <IP|URL> - Direccion del Broker MQTT");
						Serial.println("MQTTUser <usuario> - Usuario para el Broker MQTT");
						Serial.println("MQTTPasswd <contraseña> - Contraseña para el usuario del Broker MQTT");
						Serial.println("MQTTTopic <string> - Nombre para la jerarquia de los topics MQTT");
						Serial.println("SaveCom - Salvar la configuracion en el microcontrolador");
						
					}

					// Y Ya si no es de ninguno de estos ....

					else {

						Serial.println("Me ha llegado un comando que no entiendo");
						Serial.println("Comando: " + COMANDO);
						Serial.println("Payload: " + PAYLOAD);

					}

				}

				// Y si por lo que sea la libreria JSON no puede convertir el comando recibido
				else {

						Serial.println("La tarea de procesar comandos ha recibido uno que no puede deserializar.");
						
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

// Tarea para el metodo run del objeto de la cupula.
void TaskMiProyectoRun( void * parameter ){

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 100;
	xLastWakeTime = xTaskGetTickCount ();
	
	while(true){

		MiProyectoOBJ.Run();

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

// tarea para Gestion del GUI
void TaskGUI( void * parameter ){

	TickType_t xLastWakeTime;
	const TickType_t xFrequency = 100;
	xLastWakeTime = xTaskGetTickCount ();

	// Para el GUI, ya lo movere a algun sitio
	std::vector<StatusBar::WifiStatuses> wifiStatus {StatusBar::WifiStatuses::No_signal,
                                                 StatusBar::WifiStatuses::Weak,
                                                 StatusBar::WifiStatuses::Medium,
                                                 StatusBar::WifiStatuses::Full};
	int32_t uptimeHours=0;
	bool longPush = false;
	

	while(true){

		

		if (ClienteMQTT.connected()){

			BotonMQTT->SetText("OK");
		}
		else{

			BotonMQTT->SetText("FALLO");

		}
   		
   		BotonRSSI->SetText("RSSI");

   		uptimeHours = millis() / (60*60000);
   		topBar->SetUptime(uptimeHours);

   		char strftime_buf[64];
   		snprintf(strftime_buf, 64, "%02d:%02d:%02d", 12, 14, 59);
   		topBar->SetDateTime(strftime_buf);
   		
   		auto rssi =WiFi.RSSI();
   		if(rssi >= -55) {
   			topBar->SetWifiStatus(StatusBar::WifiStatuses::Full);
   		} 
		else if(rssi >= -75) {
   			topBar->SetWifiStatus(StatusBar::WifiStatuses::Medium);
   		} 
		else if(rssi >= -85) {
      		topBar->SetWifiStatus(StatusBar::WifiStatuses::Weak);
   		} 
		else {
      		topBar->SetWifiStatus(StatusBar::WifiStatuses::No_signal);
   		}
  	
		//M5.update();
		//delay(10);

  		// Notify the widgets that physical buttons are pressed
		
		M5.BtnA.read();
  		if(M5.BtnA.wasPressed()) {
	    	focus->OnButtonAPressed();
  		}

		M5.BtnB.read();
  		if(M5.BtnB.pressedFor(1000)) {
    		if(!longPush) {
      		focus->OnButtonBLongPush();
      		longPush = true;
    		}
  		}
  		
		  else if(M5.BtnB.wasReleased()) {
    		if(!longPush) {
      			focus->OnButtonBPressed();
    		}
    		else {
      			longPush = false;
    		}
  		}

		M5.BtnC.read();
  		if(M5.BtnC.wasPressed()) {
    		focus->OnButtonCPressed();
  		}

  		// Redraw the screen
  		screen->Draw();

  		
		vTaskDelayUntil( &xLastWakeTime, xFrequency );

	}
	
}

// Funcion de manejo del Timer con soporte para unidad de coma flotante
/*
uint32_t cp0_regs[18];

void IRAM_ATTR timer_isr() {

	// Para que esta funcion no sea interrupida
	portENTER_CRITICAL(&timerMux);

	 // get FPU state
  	uint32_t cp_state = xthal_get_cpenable();
  
  	if(cp_state) {
    	
		// Salvar los registros actuales de la FPU si hay
    	xthal_save_cp0(cp0_regs);

  	} 
	
	else {
    	
		// Habilitar la FPU
    	xthal_set_cpenable(1);

  	}
	
  
	// Aqui podemos hacer la cosa que queramos que haga el timer.
	
	

 	if(cp_state) {
    	
		// restaurar los registros de la FPU
    	xthal_restore_cp0(cp0_regs);

  	} 
	
	else {
    
		// Apagar la FPU
    	xthal_set_cpenable(0);

  	}

	// Deshabilitar el modo no interrupcion
	portEXIT_CRITICAL(&timerMux);

}

 */

#pragma endregion




#pragma region Funcion Setup() de ARDUINO

// funcion SETUP de Arduino
void setup() {
	
	// Puerto Serie
	Serial.begin(115200);
	Serial.println();

	Serial.println("-- Iniciando Controlador MiProyecto --");

	// Configuracion de la clase del M5Stack
	M5.begin();
	//Wire.begin();
	M5.Lcd.invertDisplay(true);
	M5.Lcd.fillScreen(BLACK);
  	M5.Lcd.setCursor(10, 10);
  	M5.Lcd.setTextColor(WHITE);
 	M5.Lcd.setTextSize(3);
	M5.Lcd.printf("INICIANDO SISTEMA");
	delay(2000);
	
	// Configuracion del GUI
	// Instanciate and configure all widgets
  	topBar = new StatusBar();
  	bottomBar = new ButtonInfoBar();
  	mosaic = new WidgetMosaic(3, 2);
  	screen = new AppScreen(Size(320, 240), BLACK, topBar, bottomBar, mosaic);

  	// Give the focus to the main screen
  	focus = screen;

  	BotonMQTT = new Codingfield::UI::Button(mosaic);
  	BotonMQTT->SetBackgroundColor(RED);
  	BotonMQTT->SetTextColor(WHITE);
  	BotonMQTT->SetTitle("MQTT");
	
  	BotonRSSI = new Codingfield::UI::Button(mosaic);
  	BotonRSSI->SetBackgroundColor(ORANGE);
  	BotonRSSI->SetTextColor(BLACK);
  	BotonRSSI->SetText("50%");

  	BotonAZ = new Codingfield::UI::UpDownButton(mosaic); // Up/Down button
  	BotonAZ->SetBackgroundColor(GREEN);
  	BotonAZ->SetTextColor(BLACK);
  	BotonAZ->SetText("0");

  	BotonInitHW = new Codingfield::UI::Button(mosaic);
  	BotonInitHW->SetBackgroundColor(TFT_GREEN);
  	BotonInitHW->SetTextColor(TFT_BLACK);
  	BotonInitHW->SetText("INIT");

  	BotonSTOP = new Codingfield::UI::Button(mosaic);
  	BotonSTOP->SetBackgroundColor(GREEN);
  	BotonSTOP->SetTextColor(TFT_RED);
  	BotonSTOP->SetText("STOP");

  	BotonPARK = new Codingfield::UI::Button(mosaic);
  	BotonPARK->SetBackgroundColor(GREEN);
  	BotonPARK->SetTextColor(TFT_BLACK);
  	BotonPARK->SetText("PARK");

  	topBar->SetUptime(0);
  	topBar->SetWifiStatus(StatusBar::WifiStatuses::No_signal);

  	bottomBar->SetButtonAText("<");
  	bottomBar->SetButtonBText("SELECT");
  	bottomBar->SetButtonCText(">");
	
  	// Callback called by the mosaic when it changes mode (mosaic/zoom on 1 widget)
  	// We use it to update the bottom bar.
  	mosaic->SetZoomOnSelectedCallback([](Widget* widget, bool edit) {
	
		if(edit) {
      		if(widget->IsEditable()){
        		bottomBar->SetButtonAText("-");
        		bottomBar->SetButtonBText("APPLY");
        		bottomBar->SetButtonCText("+");
      		} 
			else {
        		bottomBar->SetButtonAText("");
        		bottomBar->SetButtonBText("BACK");
        		bottomBar->SetButtonCText("");
      		}
    	}
		else {
      		bottomBar->SetButtonAText("<");
      		bottomBar->SetButtonBText("SELECT");
      		bottomBar->SetButtonCText(">");
    	}
 	});

  	// Configure callback to be called when the user wants to increment the value
  	// of BotonAZ
  	BotonAZ->SetUpCallback([](UpDownButton* w) {
    	editButtonValue++;
    	w->SetText(String(editButtonValue).c_str());
    	return true;
  	});

  	// Configure callback to be called when the user wants to decrement the value
  	// of BotonAZ
  	BotonAZ->SetDownCallback([](UpDownButton* w) {
    	editButtonValue--;
    	w->SetText(String(editButtonValue).c_str());
    	return true;
  	});

  	// Configure callback to be called when the user wants to apply the value
  	// of BotonAZ
  	BotonAZ->SetApplyCallback([](UpDownButton* w) {
    	editOldButtonValue = editButtonValue;
    	return false;
  	});

  	// Configure callback to be called when the user wants to cancel modification
  	// of the value of BotonAZ
  	BotonAZ->SetCancelCallback([](UpDownButton* w) {
    	editButtonValue = editOldButtonValue;
    	w->SetText(String(editButtonValue).c_str());
    	return true;
  	});

  	// Draw the screen and all its children
  	screen->Draw();
	//}

	delay(6000);

	
	// Asignar funciones Callback
	MiProyectoOBJ.SetRespondeComandoCallback(MandaRespuesta);
		
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
  			ClienteMQTT.onMessage(onMqttMessage);
  			ClienteMQTT.onPublish(onMqttPublish);
  			ClienteMQTT.setServer(MiConfig.mqttserver, 1883);
			ClienteMQTT.setCleanSession(true);
			ClienteMQTT.setClientId("M5Stack");
			ClienteMQTT.setCredentials(MiConfig.mqttusuario,MiConfig.mqttpassword);
			ClienteMQTT.setKeepAlive(4);
			ClienteMQTT.setWill(MiConfig.lwtTopic.c_str(),2,true,"Offline");

			// Tarea de gestion de la conexion MQTT. Lanzamos solo si conseguimos leer la configuracion

			xTaskCreatePinnedToCore(TaskGestionRed,"MQTT_Conectar",3000,NULL,1,&THandleTaskGestionRed,0);

	
		}

		// Leer configuracion salvada del Objeto MiProyectoOBJ
		MiProyectoOBJ.LeeConfig();

	}

	else {

		SPIFFS.begin(true);

	}

	// COLAS
	ColaComandos = xQueueCreate(10,100);
	ColaRespuestas = xQueueCreate(10,300);

	// TASKS
	Serial.println("Creando tareas del sistema.");
	
	// Tareas CORE0
	
	xTaskCreatePinnedToCore(TaskProcesaComandos,"ProcesaComandos",3000,NULL,1,&THandleTaskProcesaComandos,0);
	xTaskCreatePinnedToCore(TaskEnviaRespuestas,"EnviaMQTT",2000,NULL,1,&THandleTaskEnviaRespuestas,0);
	xTaskCreatePinnedToCore(TaskMiProyectoRun,"MiProyectoRun",2000,NULL,1,&THandleTaskMiProyectoRun,0);
	xTaskCreatePinnedToCore(TaskMandaTelemetria,"MandaTelemetria",2000,NULL,1,&THandleTaskMandaTelemetria,0);
	xTaskCreatePinnedToCore(TaskComandosSerieRun,"ComandosSerieRun",1000,NULL,1,&THandleTaskComandosSerieRun,0);
	xTaskCreatePinnedToCore(TaskGUI,"ComandosSerieRun",2000,NULL,1,&THandleTaskGUI,0);
	
	// Tareas CORE1


	// Timer

	//timer_stp = timerBegin(0, 80, true);
  	//timerAttachInterrupt(timer_stp, &timer_isr, true);
  	//timerAlarmWrite(timer_stp, TIMER_TICK_US, true);
  	//timerAlarmEnable(timer_stp);
	
	// Init Completado.
	Serial.println("Setup Completado.");

	//De momento con esto callo el click del altavoz.
	//dacWrite (25,0);
	
}

#pragma endregion

#pragma region Funcion Loop() de ARDUINO

// Funcion LOOP de Arduino
// Se ejecuta en el Core 1
// Como todo se getiona por Task aqui no se pone NADA
void loop() {
		
			
}

#pragma endregion

/// FIN DEL PROGRAMA ///