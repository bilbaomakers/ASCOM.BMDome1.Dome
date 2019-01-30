using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Reflection;



namespace ASCOM.BMDome1
{
    internal class DriverInfo
    {

        public string Titulo { get; } = "";
        public string Company { get; } = "";
        public string Copyright { get; } = "";
        

        public DriverInfo() {

            //this.Titulo = Assembly.GetEntryAssembly().GetCustomAttribute<AssemblyTitleAttribute>().Title;
            //this.Company = Assembly.GetEntryAssembly().GetCustomAttribute<AssemblyCompanyAttribute>().Company;
            //this.Copyright = Assembly.GetEntryAssembly().GetCustomAttribute<AssemblyCopyrightAttribute>().Copyright;

            this.Titulo = "ASCOM.BMDome1";
            this.Company = "BilbaoMakers 2019";
            this.Copyright = "GNU GENERAL PUBLIC LICENSE V3";


        }



    }

}
