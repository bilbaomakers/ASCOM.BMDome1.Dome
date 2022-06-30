#include <CuadroMando.h>
#include <OneButton.h> // Interesante: https://github.com/mathertel/OneButton

// Funcionalidades de Botones y Luces

// LUZ ROJA - Avisos
	// Aviso "Seta apretada" si intento HW Init - 3 pulsos cortos
	// Aviso inicio del movimiento - 1 pulso 1 segundo
	// Aviso al terminar de iniciar el sistema - 3 pulsos cortos

// LUZ VERDE - Comunicaciones
	// Conectando a la wifi - 1 pulso por segundo
	// Conectando al MQTT - 2 pulsos cortos y 1 segundo idle
	// Comunicaciones OK - Permanente

// LUZ AZUL - ESTADO DEL HW
	// HW NOT INIT - Apagado
	// HW OK READY - Encendido
	// HW READY AT PARK - 1 Pulso Segundo

// BOTON1 - AZUL
	// CLICK - Inicializa Hardware STD (busca home)
	// HOLD - Inicializa Hardware FORCE (sin buscar home)
	
// BOTON2 - VERDE
	// CLICK - Mover a 90 (SlewToAZimut)
	// HOLD - Set Park en la posicion actual (SetParkHere)
	
// BOTON3 - ROJO
	// CLICK - Parada Normal (AbortSlew)
	// HOLD - Aparcar (Park)

// BOTON4 - VERDE
	// CLICK - Mover a 270 (SlewToAZimut)
	// HOLD - 


// Constructor
CuadroMando::CuadroMando(uint8_t pinBoton1, uint8_t pinBoton2, uint8_t pinBoton3, uint8_t pinBoton4, uint8_t pinLedRojo, uint8_t pinLedVerde, uint8_t pinLedAzul, uint8_t pinSalida4, uint8_t pinSalida5, uint8_t pinSalida6, uint8_t pinSalida7){

	// Llenar el array con los pines, una vez y luego ya esta facil
	arrayPinesEntrada[0]=pinBoton1;
	arrayPinesEntrada[1]=pinBoton2;
	arrayPinesEntrada[2]=pinBoton3;
	arrayPinesEntrada[3]=pinBoton4;
	
	arrayPinesSalida[0]=pinLedRojo;
	arrayPinesSalida[1]=pinLedVerde;
	arrayPinesSalida[2]=pinLedAzul;
	arrayPinesSalida[3]=pinSalida4;
	arrayPinesSalida[4]=pinSalida5;
	arrayPinesSalida[5]=pinSalida6;
	arrayPinesSalida[6]=pinSalida7;


	// Para inicializar las entradas
	for (size_t i = 0; i < 4; i++){
		
		pinMode(arrayPinesEntrada[i], INPUT);

	}

	// Para inicializar las Salidas
	for (size_t i = 0; i < 7; i++){

		pinMode(arrayPinesSalida[i], OUTPUT);
		digitalWrite(arrayPinesSalida[i], LOW);

	}

	
	ledRojo = IndicadorLed(arrayPinesSalida[0], false);
	ledVerde = IndicadorLed(arrayPinesSalida[1], false);
	ledAzul = IndicadorLed(arrayPinesSalida[2], false);

	boton1 = OneButton(pinBoton1,true,true);
	boton2 = OneButton(pinBoton2,true,true);
	boton3 = OneButton(pinBoton3,true,true);
	boton4 = OneButton(pinBoton4,true,true);
	
	// Parametros para los botones
  	boton1.setDebounceTicks(20); // ms de debounce
  	boton1.setPressTicks(500); // ms para HOLD
	boton2.setDebounceTicks(20); // ms de debounce
  	boton2.setPressTicks(500); // ms para HOLD
	boton3.setDebounceTicks(20); // ms de debounce
  	boton3.setPressTicks(500); // ms para HOLD
	boton4.setDebounceTicks(20); // ms de debounce
  	boton4.setPressTicks(500); // ms para HOLD
	
	//boton1.attachClick(handleClickBoton1);
	//boton2.attachClick(handleClickBoton2);
	


}

void CuadroMando::TestSalidas(){

	for (size_t i = 0; i < 7; i++)
	{
		digitalWrite(arrayPinesSalida[i], HIGH);
		delay(2000);
		digitalWrite(arrayPinesSalida[i], LOW);
	}

}

void CuadroMando::setEnviaComandoCallback(enviaComandoCallback ref){

	miEnviaComando = (enviaComandoCallback)ref;

}

void CuadroMando::handleClickBoton1(){

	miEnviaComando("InitHW","STD");

}

void CuadroMando::handleHoldBoton1(){

	miEnviaComando("InitHW","FORCE");

}

void CuadroMando::handleClickBoton2(){
		
	miEnviaComando("SlewToAZimut","90");

}

void CuadroMando::handleHoldBoton2(){

	this->miEnviaComando("SetParkHere","NA");

}

void CuadroMando::handleClickBoton3(){

	this->miEnviaComando("AbortSlew","NA");

}

void CuadroMando::handleHoldBoton3(){
	
	this->miEnviaComando("Park","NA");

}

void CuadroMando::handleClickBoton4(){

	this->miEnviaComando("SlewToAZimut","270");

}

void CuadroMando::handleHoldBoton4(){

}

void CuadroMando::Run(){

	boton1.tick();
	boton2.tick();
	boton3.tick();
	boton4.tick();
	ledRojo.RunFast();
	ledVerde.RunFast();
	ledAzul.RunFast();

}