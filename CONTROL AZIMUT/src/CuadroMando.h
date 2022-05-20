#pragma once
#include <IndicadorLed.h>				// Mi libreria para objetos LED

class CuadroMando{

	private:

		// Array con el numero de Pin de cada entrtada, de la 0 a 5 y las salidas de la 0 a 6
		uint8_t arrayPinesEntrada[6];
		uint8_t arrayPinesSalida[7];


	public:

		CuadroMando(uint8_t pinBoton1, uint8_t pinBoton2, uint8_t pinBoton3, uint8_t pinBoton4, uint8_t pinEmergencyStop, uint8_t pinHomeSensor, uint8_t pinLedRojo, uint8_t pinLedVerde, uint8_t pinLedAzul, uint8_t pinSalida4, uint8_t pinSalida5, uint8_t pinSalida6, uint8_t pinSalida7);		// Constructor (es la funcion que devuelve un Objeto de esta clase)
		~CuadroMando() {};	// Destructor (Destruye el objeto, o sea, lo borra de la memoria)
		void TestSalidas(); // Ciclo de test de las salidas. OJO que es BLOQUEANTE
		void Run(); // Ciclo de vida de la clase
		IndicadorLed ledRojo;
		IndicadorLed ledVerde;
		IndicadorLed ledAzul;
		
};