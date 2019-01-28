using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ASCOM.BMDome1
{
    internal static class Extensions
    {

        public static string Json(this DriverInfo value) {


            return ASCOM.BMDome1.Json.Serialize(value);


        }

    }
}
