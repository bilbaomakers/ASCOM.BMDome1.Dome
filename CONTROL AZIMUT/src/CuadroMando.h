#pragma once
#include <IndicadorLed.h>				// Mi libreria para objetos LED
#include <OneButton.h> 					// Interesante: https://github.com/mathertel/OneButton

class CuadroMando{

	private:

		// Array con el numero de Pin de cada entrtada, de la 0 a 5 y las salidas de la 0 a 6
		uint8_t arrayPinesEntrada[4];
		uint8_t arrayPinesSalida[7];

		// La callback esta aqui me va a pasar tambien la instancia del objeto
		// Me hace falta para llamar a los metodos miembros de esta clase desde las callback de los botones
		// porque tienen que ser static y luego dentro no puedo hacer nada sin la instancia real del objeto cuadro
		// porque al ser static el this-> no vale, necesito el objeto.
		CuadroMando *miInstanciaCuadro;
		typedef void(*enviaComandoCallback)(String l_comando, String l_payload);	// Callback para enviar comandos a la cola de comandos
		enviaComandoCallback miEnviaComando = nullptr;								

		// Objetos para el manejo de botones
		OneButton boton1;
		OneButton boton2;
		OneButton boton3;
		OneButton boton4;

		// Funciones Callback de los botones
		/*
		static void handleClickBoton1();
		static void handleHoldBoton1();
		static void handleClickBoton2();
		static void handleHoldBoton2();
		static void handleClickBoton3();
		static void handleHoldBoton3();
		static void handleClickBoton4();
		static void handleHoldBoton4();
		*/

		String t_comandoAEnviar = "";
		String t_parametroAEnviar;


	public:
		
		CuadroMando(uint8_t pinBoton1, uint8_t pinBoton2, uint8_t pinBoton3, uint8_t pinBoton4, uint8_t pinLedRojo, uint8_t pinLedVerde, uint8_t pinLedAzul, uint8_t pinSalida4, uint8_t pinSalida5, uint8_t pinSalida6, uint8_t pinSalida7);		// Constructor (es la funcion que devuelve un Objeto de esta clase)
		~CuadroMando() {};	// Destructor (Destruye el objeto, o sea, lo borra de la memoria)
		void TestSalidas(); // Ciclo de test de las salidas. OJO que es BLOQUEANTE
		void Run(); // Ciclo de vida de la clase
		void setEnviaComandoCallback(enviaComandoCallback ref);	// Definir la funcion para pasarnos la funcion de callback del enviamensajes
		IndicadorLed ledRojo;
		IndicadorLed ledVerde;
		IndicadorLed ledAzul;
		
};