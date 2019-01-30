using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows.Forms;
using ASCOM.Utilities;
using ASCOM.BMDome1;
using MQTTnet;
using MQTTnet.Client;
using MQTTnet.Diagnostics;


namespace ASCOM.BMDome1
{
    [ComVisible(false)]					// Form not registered for COM!
    public partial class SetupDialogForm : Form
    {

        Configuracion miconfiguracion;
        

        internal static string driverID = "ASCOM.BMDome1.Dome";


        public SetupDialogForm()
        {
            InitializeComponent();
            // Initialise current values of user settings from the ASCOM Profile
            InitUI();

        }

        

        private void BrowseToAscom(object sender, EventArgs e) // Click on ASCOM logo event handler
        {
            try
            {
                System.Diagnostics.Process.Start("http://ascom-standards.org/");
            }
            catch (System.ComponentModel.Win32Exception noBrowser)
            {
                if (noBrowser.ErrorCode == -2147467259)
                    MessageBox.Show(noBrowser.Message);
            }
            catch (System.Exception other)
            {
                MessageBox.Show(other.Message);
            }
        }



        private void BrowseToBM(object sender, EventArgs e) // Click on ASCOM logo event handler
        {
            try
            {
                System.Diagnostics.Process.Start("https://bilbaomakers.org/");
            }
            catch (System.ComponentModel.Win32Exception noBrowser)
            {
                if (noBrowser.ErrorCode == -2147467259)
                    MessageBox.Show(noBrowser.Message);
            }
            catch (System.Exception other)
            {
                MessageBox.Show(other.Message);
            }
        }



        private void InitUI()
        {

            this.CmdPruebaCom.Click += CmdPruebaCom_Click;
            this.picASCOM.Click += BrowseToAscom;
            this.picBM.Click += BrowseToBM;
            this.cmdOK.Click += CmdOK_Click;

            // Leer la configuracion del driver



            miconfiguracion = new Configuracion(driverID);

            TxtIdCliente.Text = miconfiguracion.IdCliente;
            TxtServidor.Text = miconfiguracion.ServidorMQTT;
            TxtPuerto.Text = miconfiguracion.Puerto;
            TxtUsuario.Text = miconfiguracion.Usuario;
            TxtPassword.Text = miconfiguracion.Password;
            TxtTopicBase.Text = miconfiguracion.TopicBase;
                                               
        }

       

        private void CmdOK_Click(object sender, EventArgs e)
        {

            miconfiguracion.IdCliente = TxtIdCliente.Text;
            miconfiguracion.ServidorMQTT = TxtServidor.Text;
            miconfiguracion.Puerto = TxtPuerto.Text;
            miconfiguracion.Usuario = TxtUsuario.Text;
            miconfiguracion.Password = TxtPassword.Text;
            miconfiguracion.TopicBase = TxtTopicBase.Text;

            miconfiguracion.Guardar();
            
                        
        }

        private void CmdPruebaCom_Click(object sender, EventArgs e)
        {


            

        MqttClient lMqttClient;
        MqttFactory lMqttFactory;
        IMqttClientOptions lIMqttClientOptions;

        lMqttFactory = new MqttFactory();
            lMqttClient = (MqttClient)lMqttFactory.CreateMqttClient();

            lIMqttClientOptions = (IMqttClientOptions)new MqttClientOptionsBuilder()
              .WithClientId(TxtIdCliente.Text)
              .WithTcpServer(TxtServidor.Text)
              .WithCredentials(TxtUsuario.Text, TxtPassword.Text)
              .WithCleanSession()
              .Build();


            try
            {

                lMqttClient.Connected += delegate (object client, MqttClientConnectedEventArgs e1)
                {


                    DriverInfo lDriverInfo = new DriverInfo();


                    //Configuracion b = Json.Deserialize<Configuracion>(a);
                    //Json.Serialize(miconfiguracion);

                    lMqttClient.PublishAsync(TxtTopicBase.Text + "/INFO", lDriverInfo.Json());

                    MessageBox.Show("Conexion Satisfactoria Con el servidor MQTT", "Conexion al servidor " + this.TxtServidor.Text );
                    

                };


                lMqttClient.ConnectAsync(lIMqttClientOptions);

            }


            catch (MQTTnet.Exceptions.MqttCommunicationException f)
            {

                MessageBox.Show(f.Message);

            }


        }


    }
}