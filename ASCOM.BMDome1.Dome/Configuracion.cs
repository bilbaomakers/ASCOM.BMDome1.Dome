using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Xml.Serialization;



namespace ASCOM.BMDome1
{
    public class Configuracion
    {

        public string IDCliente = "";
        public string ServidorMQTT = "";
        public string Puerto = "";
        public string Usuario = "";
        public string Password = "";
        public string TopicBase = "";
        
        private const string Fichero = "BMDomo1config.xml";

        private static string RutaConfiguracion
        {
            get
            {
                string lstrRuta = "";
                lstrRuta = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), FileVersionInfo.GetVersionInfo(Assembly.GetExecutingAssembly().Location).ProductName);
                if (!Directory.Exists(lstrRuta)) Directory.CreateDirectory(lstrRuta);
                return lstrRuta;
            }
        }

        public static Configuracion Cargar()
        {
            if (!File.Exists(Path.Combine(Configuracion.RutaConfiguracion, Configuracion.Fichero)))
            {
                return new Configuracion();
            }
            else
            {
                return Configuracion.fromXml(File.ReadAllText(Path.Combine(Configuracion.RutaConfiguracion, Configuracion.Fichero)));
            }
        }

        public void Guardar()
        {
            File.WriteAllText(Path.Combine(Configuracion.RutaConfiguracion, Configuracion.Fichero), this.toXml());
        }

        public static bool Tiene
        {
            get
            {
                Configuracion lobjConfiguracion = null;
                lobjConfiguracion = Configuracion.Cargar();
                if (lobjConfiguracion == null)
                {
                    return false;
                }
                else
                {
                    if (string.IsNullOrEmpty(lobjConfiguracion.IDCliente) || string.IsNullOrEmpty(lobjConfiguracion.ServidorMQTT) || string.IsNullOrEmpty(lobjConfiguracion.Puerto) || string.IsNullOrEmpty(lobjConfiguracion.Usuario) || string.IsNullOrEmpty(lobjConfiguracion.Password) || string.IsNullOrEmpty(lobjConfiguracion.TopicBase))
                    {
                        return false;
                    }
                    else
                    {
                        return true;
                    }
                }
            }
        }

        private string toXml()
        {
            XmlSerializer xml_serializer = new XmlSerializer(typeof(Configuracion));
            StringWriter string_writer = new StringWriter();

            try
            {
                xml_serializer.Serialize(string_writer, this);

                return string_writer.ToString();
            }
            catch (Exception ex)
            {
                throw ex;
            }
            finally
            {
                xml_serializer = null;
                string_writer = null;
            }
        }

        private static Configuracion fromXml(string xml)
        {
            XmlSerializer xml_serializer = new XmlSerializer(typeof(Configuracion));
            StringReader string_reader = new StringReader(xml);

            try
            {
                return (Configuracion)xml_serializer.Deserialize(string_reader);
            }
            catch
            {
                return null;
            }
            finally
            {
                xml_serializer = null;
                string_reader = null;
            }
        }


    }
}
