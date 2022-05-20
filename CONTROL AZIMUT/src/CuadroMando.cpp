#include <CuadroMando.h>


// Constructor
CuadroMando::CuadroMando(uint8_t pinBoton1, uint8_t pinBoton2, uint8_t pinBoton3, uint8_t pinBoton4, uint8_t pinEmergencyStop, uint8_t pinHomeSensor, uint8_t pinLedRojo, uint8_t pinLedVerde, uint8_t pinLedAzul, uint8_t pinSalida4, uint8_t pinSalida5, uint8_t pinSalida6, uint8_t pinSalida7){

	// Llenar el array con los pines, una vez y luego ya esta facil
	arrayPinesEntrada[0]=pinBoton1;
	arrayPinesEntrada[1]=pinBoton2;
	arrayPinesEntrada[2]=pinBoton3;
	arrayPinesEntrada[3]=pinBoton4;
	arrayPinesEntrada[4]=pinEmergencyStop;
	arrayPinesEntrada[5]=pinHomeSensor;

	arrayPinesSalida[0]=pinLedRojo;
	arrayPinesSalida[1]=pinLedVerde;
	arrayPinesSalida[2]=pinLedAzul;
	arrayPinesSalida[3]=pinSalida4;
	arrayPinesSalida[4]=pinSalida5;
	arrayPinesSalida[5]=pinSalida6;
	arrayPinesSalida[6]=pinSalida7;
		
	// Para inicializar las entradas
	for (size_t i = 0; i < 6; i++){
		
		pinMode(arrayPinesEntrada[i], INPUT);

	}

	// Para inicializar las Salidas
	for (size_t i = 0; i < 7; i++){

		pinMode(arrayPinesSalida[i], OUTPUT);
		digitalWrite(arrayPinesSalida[i], LOW);

	}

	
	ledRojo = IndicadorLed(arrayPinesSalida[0], true);
	ledVerde = IndicadorLed(arrayPinesSalida[1], true);
	ledAzul = IndicadorLed(arrayPinesSalida[2], true);
	 		
}

void CuadroMando::TestSalidas(){

	for (size_t i = 0; i < 7; i++)
	{
		digitalWrite(arrayPinesSalida[i], HIGH);
		delay(500);
		digitalWrite(arrayPinesSalida[i], LOW);
		delay(500);
	}

}

void CuadroMando::Run(){


}