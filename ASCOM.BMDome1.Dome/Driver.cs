//tabs=4


// Driver ASCOM para BMDome1
//
// Description:	Driver para controlador de Domo desarrollado por Bilbaomakers
//				para el proyecto de observatorio astronomico en Marcilla de Campos.
//				Control de azimut de la cupula y apertura cierre del shutter.
//				Hardware de control con Arduino y ESP8266
//				Comunicaciones mediante MQTT
//
// Implements:	ASCOM Dome interface version: 6.4SP1
// Author:		Diego Maroto - BilbaoMakers 2019 - info@bilbaomakers.org




#define Dome

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Text;
using System.Runtime.InteropServices;

using ASCOM;
using ASCOM.Astrometry;
using ASCOM.Astrometry.AstroUtils;
using ASCOM.Utilities;
using ASCOM.DeviceInterface;
using System.Globalization;
using System.Collections;

using MQTTnet;     
using MQTTnet.Client; // https://github.com/chkr1011/MQTTnet/wiki/Client
using MQTTnet.Diagnostics;



namespace ASCOM.BMDome1
{
    //
    // DeviceID is ASCOM.BMDome1.Dome
    //
    // The Guid attribute sets the CLSID for ASCOM.BMDome1.Dome
    // The ClassInterface/None addribute prevents an empty interface called
    // _BMDome1 from being created and used as the [default] interface
    //
    // TODO Replace the not implemented exceptions with code to implement the function or
    // throw the appropriate ASCOM exception.
    //

    /// <summary>
    /// ASCOM Dome Driver for BMDome1.
    /// </summary>
    [Guid("c3feed38-bf1d-4de8-9f7c-be4daa094aba")]
    [ClassInterface(ClassInterfaceType.None)]
    public class Dome : IDomeV2
    {

        // Mis Objetos para este ambito (el del driver, u sea todo ....)

        // Info
        internal static string driverID = "ASCOM.BMDome1.Dome";
        private static string driverDescription = "Domo BilbaoMakers Version 1.0.0";
                
        // Para mi gestor de configuraciones
        private Configuracion miconfiguracion;

        // Para las comunicaciones
        private MqttClient lMqttClient;
        private MqttFactory lMqttFactory;
        private IMqttClientOptions lIMqttClientOptions;
        private MqttApplicationMessage MensajeLWT;
        
        private static readonly string TopicStatus = "/STAUS";
        private static readonly string TopicComando = "/CMD";
        private static readonly string TopicLWTDriver = "/DRIVER/LWT";
        private static readonly string TopicLWTHardware = "/HARDWARE/LWT";

        // Variable para almacenar el estado conectado
        private bool connectedState;
        
        //private AstroUtils astroUtilities;

        // El tracelogger, que me lo ha creado el asistente y de momento no se donde lo tira ya mirare a ver
        internal static TraceLogger tl;


        // Constructor de la clase
        public Dome()
        {
            // El tracelogger
            tl = new TraceLogger("", "BMDome1");
            
            // mensajito de iniciando
            tl.LogMessage("Dome", "Starting initialisation");


            connectedState = false; // Initialise connected to false
            // No lo uso borrar
            //utilities = new Util(); //Initialise util object
            //astroUtilities = new AstroUtils(); // Initialise astro utilities object
            //TODO: Implement your additional construction here


            // En el constructor del Driver inicializo objetos que me hagan falta luego

            // Mi Configuracion
            miconfiguracion = new Configuracion(driverID);

            // Los objetos del MQTT
            lMqttFactory = new MqttFactory();
            lMqttClient = (MqttClient)lMqttFactory.CreateMqttClient();
            MensajeLWT = new MqttApplicationMessage();

            


            // Incluido el mensajito de LWT
            MensajeLWT.Topic = miconfiguracion.TopicBase + TopicLWTDriver;
            MensajeLWT.Payload = Encoding.ASCII.GetBytes("Offline");      // Mensaje LWT de Offline
            MensajeLWT.Retain = true;
            MensajeLWT.QualityOfServiceLevel = MQTTnet.Protocol.MqttQualityOfServiceLevel.ExactlyOnce;
            

            // Y opciones de la comunicacion MQTT
            lIMqttClientOptions = (IMqttClientOptions)new MqttClientOptionsBuilder()
              .WithClientId(miconfiguracion.IdCliente)
              .WithTcpServer(miconfiguracion.ServidorMQTT)
              .WithCredentials(miconfiguracion.Usuario, miconfiguracion.Password)
              .WithWillMessage(MensajeLWT)
              .WithCommunicationTimeout(new TimeSpan(5000))
              .WithKeepAlivePeriod(new TimeSpan(10000))
              .WithCleanSession()
              .Build();
          


            tl.LogMessage("Dome", "Completed initialisation");
        }
                       
        
        #region Propiedades y Metodos

        // El formulario de SETUP
        // El unico sitio donde el driver puede interactuar con el usuario.
        public void SetupDialog()
        {
            // consider only showing the setup dialog if not connected
            // or call a different dialog if connected
            if (IsConnected)
                System.Windows.Forms.MessageBox.Show("Estas conectado al domo. Desconectate para configurar");

            using (SetupDialogForm F = new SetupDialogForm())
            {
                var result = F.ShowDialog();
                if (result == System.Windows.Forms.DialogResult.OK)
                {
                    //WriteProfile(); // Persist device configuration values to the ASCOM Profile store
                }
            }
        }
        
        // De momento no devolvemos ninguna "Supported Action" extra
        public ArrayList SupportedActions
        {
            get
            {
                tl.LogMessage("SupportedActions Get", "Returning empty arraylist");
                return new ArrayList();
            }
        }

        // Y por lo tanto ni metodos ni nada, devolvemos excepcion para que el software sepa que no lo permite este driver.
        public string Action(string actionName, string actionParameters)
        {
            throw new ASCOM.ActionNotImplementedException("Action " + actionName + " is not implemented by this driver");
        }


        // Esto no nos interesa de momento
        public void CommandBlind(string command, bool raw)
        {
            CheckConnected("CommandBlind");
            // Call CommandString and return as soon as it finishes
            this.CommandString(command, raw);
            // or
            throw new ASCOM.MethodNotImplementedException("CommandBlind");
            // DO NOT have both these sections!  One or the other
        }

        // Esto no nos interesa de momento
        public bool CommandBool(string command, bool raw)
        {
            CheckConnected("CommandBool");
            string ret = CommandString(command, raw);
            // TODO decode the return string and return true or false
            // or
            throw new ASCOM.MethodNotImplementedException("CommandBool");
            // DO NOT have both these sections!  One or the other
        }



        // AQUI SE SUPONE QUE HAY QUE PONER LA COMUNICACIONES CON EL HW y llamar esta funcion segun recomienda ASCOM
        public string CommandString(string command, bool raw)

        {
            CheckConnected("CommandString");
            // it's a good idea to put all the low level communication with the device here,
            // then all communication calls this function
            // you need something to ensure that only one command is in progress at a time

            // QUITAR CUANDO IMPLEMENTADO
            throw new ASCOM.MethodNotImplementedException("CommandString");


        }
               

        // Esto es para destruir la instancia del Driver. Lo dejamos como esta de momento.
        public void Dispose()
        {
            // Clean up the tracelogger and util objects
            tl.Enabled = false;
            tl.Dispose();
            tl = null;
            
        }


        // La propiedad Conected. Es el estado de la Variable PRIVADA IsConected, y lo que hay que hacer para conectar.
        public bool Connected
        {
            // Leerla es facil .....
            get
            {
                tl.LogMessage("Connected", "Get {0}", IsConnected);
                return IsConnected;
            }



            // Cambiar su valor implica hablar con el HW.
            set
            {
                tl.LogMessage("Connected", "Set {0}", value);
                if (value == IsConnected)
                    return;


                // Aqui abro la conexion si no lo esta con el servidor MQTT y hablo con el Arduino principal
                // Si en el set me estan poniendo el valor a true .....
                if (value)
                {
                    
                    
                    // Intentar conexion con servidor MQTT
                    try
                    {

                        
                        
                        // Callback de lo que hace cuando conecta
                        lMqttClient.Connected += delegate (object client, MqttClientConnectedEventArgs e1)
                        {

                            // Json de INFO
                            DriverInfo lDriverInfo = new DriverInfo();
                            // Y publicarlo
                            lMqttClient.PublishAsync(miconfiguracion.TopicBase + "/INFO", lDriverInfo.Json());
                            // Publicar un ONLINE en el LWT
                            lMqttClient.PublishAsync(miconfiguracion.TopicBase + "/DRIVER/LWT", "Online", MQTTnet.Protocol.MqttQualityOfServiceLevel.ExactlyOnce, true);

                            // Suscribirme al topic de los comandos
                            lMqttClient.SubscribeAsync(TopicComando, MQTTnet.Protocol.MqttQualityOfServiceLevel.ExactlyOnce);
                            // Y al LWT que publica el HW
                            lMqttClient.SubscribeAsync(TopicLWTHardware, MQTTnet.Protocol.MqttQualityOfServiceLevel.ExactlyOnce);


                        };


                        // Conectar
                        lMqttClient.ConnectAsync(lIMqttClientOptions);


                        // Y si todo OK .....                          
                        connectedState = true;
                        tl.LogMessage("Connected Set", "Conectado al control");

                    }



                    // Si falla la conexion con servidor MQTT .....
                    catch (MQTTnet.Exceptions.MqttCommunicationException f)
                    {

                        connectedState = false;
                        tl.LogMessage("Connected Set", "ERROR Conectando al control");

                    }

                
                }


                // Si me estan poniendo el valor a False (orden de desconectar)
                else
                {
                    connectedState = false;
                    tl.LogMessage("Connected Set", "Desconectando del control");

                    // Desconectar del HW
                    lMqttClient.PublishAsync(miconfiguracion.TopicBase + "/DRIVER/LWT", "Offline", MQTTnet.Protocol.MqttQualityOfServiceLevel.ExactlyOnce, true);

                    lMqttClient.PublishAsync(miconfiguracion.TopicBase + "/INFO", "Sayonara Baby ....");
                    lMqttClient.DisconnectAsync();
                    
                    // Nota El servidor manda Automaticamente el LWT Offine

                }

            }
        }

        public string Description
        {
            // TODO customise this device description
            get
            {
                tl.LogMessage("Description Get", driverDescription);
                return driverDescription;
            }
        }

        public string DriverInfo
        {
            get
            {
                Version version = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
                // TODO customise this driver description
                string driverInfo = "Information about the driver itself. Version: " + String.Format(CultureInfo.InvariantCulture, "{0}.{1}", version.Major, version.Minor);
                tl.LogMessage("DriverInfo Get", driverInfo);
                return driverInfo;
            }
        }

        public string DriverVersion
        {
            get
            {
                Version version = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
                string driverVersion = String.Format(CultureInfo.InvariantCulture, "{0}.{1}", version.Major, version.Minor);
                tl.LogMessage("DriverVersion Get", driverVersion);
                return driverVersion;
            }
        }

        public short InterfaceVersion
        {
            // set by the driver wizard
            get
            {
                tl.LogMessage("InterfaceVersion Get", "2");
                return Convert.ToInt16("2");
            }
        }

        public string Name
        {
            get
            {
                string name = "Short driver name - please customise";
                tl.LogMessage("Name Get", name);
                return name;
            }
        }

        #endregion



        #region Implementacion IDome

        private bool domeShutterState = false; // Variable to hold the open/closed status of the shutter, true = Open

        public void AbortSlew()
        {
            // This is a mandatory parameter but we have no action to take in this simple driver
            tl.LogMessage("AbortSlew", "Completed");
        }

        public double Altitude
        {
            get
            {
                tl.LogMessage("Altitude Get", "Not implemented");
                throw new ASCOM.PropertyNotImplementedException("Altitude", false);
            }
        }

        public bool AtHome
        {
            get
            {
                tl.LogMessage("AtHome Get", "Not implemented");
                throw new ASCOM.PropertyNotImplementedException("AtHome", false);
            }
        }

        public bool AtPark
        {
            get
            {
                tl.LogMessage("AtPark Get", "Not implemented");
                throw new ASCOM.PropertyNotImplementedException("AtPark", false);
            }
        }

        public double Azimuth
        {
            get
            {
                tl.LogMessage("Azimuth Get", "Not implemented");
                throw new ASCOM.PropertyNotImplementedException("Azimuth", false);
            }
        }

        public bool CanFindHome
        {
            get
            {
                tl.LogMessage("CanFindHome Get", false.ToString());
                return false;
            }
        }

        public bool CanPark
        {
            get
            {
                tl.LogMessage("CanPark Get", false.ToString());
                return false;
            }
        }

        public bool CanSetAltitude
        {
            get
            {
                tl.LogMessage("CanSetAltitude Get", false.ToString());
                return false;
            }
        }

        public bool CanSetAzimuth
        {
            get
            {
                tl.LogMessage("CanSetAzimuth Get", true.ToString());
                return true;
            }
        }

        public bool CanSetPark
        {
            get
            {
                tl.LogMessage("CanSetPark Get", false.ToString());
                return false;
            }
        }

        public bool CanSetShutter
        {
            get
            {
                tl.LogMessage("CanSetShutter Get", true.ToString());
                return true;
            }
        }

        public bool CanSlave
        {
            get
            {
                tl.LogMessage("CanSlave Get", false.ToString());
                return false;
            }
        }

        public bool CanSyncAzimuth
        {
            get
            {
                tl.LogMessage("CanSyncAzimuth Get", false.ToString());
                return false;
            }
        }

        public void CloseShutter()
        {
            tl.LogMessage("CloseShutter", "Shutter has been closed");
            domeShutterState = false;
        }

        public void FindHome()
        {
            tl.LogMessage("FindHome", "Not implemented");
            throw new ASCOM.MethodNotImplementedException("FindHome");
        }

        public void OpenShutter()
        {
            tl.LogMessage("OpenShutter", "Shutter has been opened");
            domeShutterState = true;
        }

        public void Park()
        {
            tl.LogMessage("Park", "Not implemented");
            throw new ASCOM.MethodNotImplementedException("Park");
        }

        public void SetPark()
        {
            tl.LogMessage("SetPark", "Not implemented");
            throw new ASCOM.MethodNotImplementedException("SetPark");
        }

        public ShutterState ShutterStatus
        {
            get
            {
                tl.LogMessage("ShutterStatus Get", false.ToString());
                if (domeShutterState)
                {
                    tl.LogMessage("ShutterStatus", ShutterState.shutterOpen.ToString());
                    return ShutterState.shutterOpen;
                }
                else
                {
                    tl.LogMessage("ShutterStatus", ShutterState.shutterClosed.ToString());
                    return ShutterState.shutterClosed;
                }
            }
        }

        public bool Slaved
        {
            get
            {
                tl.LogMessage("Slaved Get", false.ToString());
                return false;
            }
            set
            {
                tl.LogMessage("Slaved Set", "not implemented");
                throw new ASCOM.PropertyNotImplementedException("Slaved", true);
            }
        }

        public void SlewToAltitude(double Altitude)
        {
            tl.LogMessage("SlewToAltitude", "Not implemented");
            throw new ASCOM.MethodNotImplementedException("SlewToAltitude");
        }

        public void SlewToAzimuth(double Azimuth)
        {
            tl.LogMessage("SlewToAzimuth", "Not implemented");
            throw new ASCOM.MethodNotImplementedException("SlewToAzimuth");
        }

        public bool Slewing
        {
            get
            {
                tl.LogMessage("Slewing Get", false.ToString());
                return false;
            }
        }

        public void SyncToAzimuth(double Azimuth)
        {
            tl.LogMessage("SyncToAzimuth", "Not implemented");
            throw new ASCOM.MethodNotImplementedException("SyncToAzimuth");
        }

        #endregion



        #region Metodos y propiedades PRIVADOS
        

        #region ASCOM Registration

        // Register or unregister driver for ASCOM. This is harmless if already
        // registered or unregistered. 
        //
        /// <summary>
        /// Register or unregister the driver with the ASCOM Platform.
        /// This is harmless if the driver is already registered/unregistered.
        /// </summary>
        /// <param name="bRegister">If <c>true</c>, registers the driver, otherwise unregisters it.</param>
        private static void RegUnregASCOM(bool bRegister)
        {
            using (var P = new ASCOM.Utilities.Profile())
            {
                P.DeviceType = "Dome";
                if (bRegister)
                {
                    P.Register(driverID, driverDescription);
                }
                else
                {
                    P.Unregister(driverID);
                }
            }
        }

        /// <summary>
        /// This function registers the driver with the ASCOM Chooser and
        /// is called automatically whenever this class is registered for COM Interop.
        /// </summary>
        /// <param name="t">Type of the class being registered, not used.</param>
        /// <remarks>
        /// This method typically runs in two distinct situations:
        /// <list type="numbered">
        /// <item>
        /// In Visual Studio, when the project is successfully built.
        /// For this to work correctly, the option <c>Register for COM Interop</c>
        /// must be enabled in the project settings.
        /// </item>
        /// <item>During setup, when the installer registers the assembly for COM Interop.</item>
        /// </list>
        /// This technique should mean that it is never necessary to manually register a driver with ASCOM.
        /// </remarks>
        [ComRegisterFunction]
        public static void RegisterASCOM(Type t)
        {
            RegUnregASCOM(true);
        }

        /// <summary>
        /// This function unregisters the driver from the ASCOM Chooser and
        /// is called automatically whenever this class is unregistered from COM Interop.
        /// </summary>
        /// <param name="t">Type of the class being registered, not used.</param>
        /// <remarks>
        /// This method typically runs in two distinct situations:
        /// <list type="numbered">
        /// <item>
        /// In Visual Studio, when the project is cleaned or prior to rebuilding.
        /// For this to work correctly, the option <c>Register for COM Interop</c>
        /// must be enabled in the project settings.
        /// </item>
        /// <item>During uninstall, when the installer unregisters the assembly from COM Interop.</item>
        /// </list>
        /// This technique should mean that it is never necessary to manually unregister a driver from ASCOM.
        /// </remarks>
        [ComUnregisterFunction]
        public static void UnregisterASCOM(Type t)
        {
            RegUnregASCOM(false);
        }

        #endregion



        #region COMUNICACIONES CON EL HARDWARE

        // Estrujandome la sesera para ver como comunicar a la perfeccion con el cacharro
        private class Comando
        {






        }

             



        #endregion


        /// <summary>
        /// Returns true if there is a valid connection to the driver hardware
        /// </summary>
        private bool IsConnected
        {
            get
            {
                // TODO check that the driver hardware connection exists and is connected to the hardware
                return connectedState;
            }
        }

        /// <summary>
        /// Use this function to throw an exception if we aren't connected to the hardware
        /// </summary>
        /// <param name="message">Mensaje</param>
        private void CheckConnected(string message)
        {
            if (!IsConnected)
            {
                throw new ASCOM.NotConnectedException(message);
            }
        }


               
        #endregion
    }

}
