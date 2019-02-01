using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using ASCOM.Utilities;



namespace ASCOM.BMDome1
{
    public class Configuracion
    {

        public string IdCliente { get; set; } = "";
        public string ServidorMQTT { get; set; } = "";
        public string Puerto { get; set; } = "";
        public string Usuario { get; set; } = "";
        public string Password { get; set; } = "";
        public string TopicBase { get; set; } = "";
        public string QoS { get; set; } = "";

        private string _DriverId;


        public Configuracion(string DriverId) {

            _DriverId = DriverId;

            using (Profile driverProfile = new Profile(false))
            {
                driverProfile.DeviceType = "Dome";
                foreach (PropertyInfo prop in this.GetType().GetProperties())
                {
                    prop.SetValue(this, driverProfile.GetValue(DriverId, prop.Name));
                }
            }


        }


        public void Guardar()
        {

            using (Profile driverProfile = new Profile(false))
            {
                driverProfile.DeviceType = "Dome";
                foreach (PropertyInfo prop in this.GetType().GetProperties())
                {
                    driverProfile.WriteValue(_DriverId, prop.Name, prop.GetValue(this).ToString());
                }
            }


        }
        
    }
}
