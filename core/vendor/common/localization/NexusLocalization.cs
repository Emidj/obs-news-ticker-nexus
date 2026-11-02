// SPDX-License-Identifier: GPL-2.0-or-later
// Shared NEXUS locale policy and reader for the same UTF-8 catalogs used by Qt.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.Serialization.Json;
using System.Text;
namespace Nexus.Localization {
    public sealed class Catalog {
        public static readonly string[] Languages={"en-US","es-ES","it-IT","fr-FR","de-DE"};
        readonly Dictionary<string,Dictionary<string,string>> catalogs=new Dictionary<string,Dictionary<string,string>>();
        public static string LanguageFor(string locale) {
            var prefix=(locale ?? "").Trim().Replace('_','-').Split('-')[0].ToLowerInvariant();
            foreach(var code in Languages)if(code.Substring(0,2)==prefix)return code;
            return "en-US";
        }
        public Catalog(Assembly assembly,string resourcePrefix) {
            foreach(var code in Languages) {
                var values=new Dictionary<string,string>();catalogs.Add(code,values);
                using(var stream=assembly.GetManifestResourceStream(resourcePrefix+code+".ini")) {
                    if(stream==null)continue;
                    using(var reader=new StreamReader(stream,Encoding.UTF8)) {
                        string line;while((line=reader.ReadLine())!=null) {
                            int split=line.IndexOf('=');if(split<=0 || line.StartsWith("#"))continue;
                            using(var json=new MemoryStream(Encoding.UTF8.GetBytes(line.Substring(split+1))))
                                values.Add(line.Substring(0,split),(string)new DataContractJsonSerializer(typeof(string)).ReadObject(json));
                        }
                    }
                }
            }
        }
        public string Text(string key,params object[] arguments) {
            return TextIn(LanguageFor(CultureInfo.CurrentUICulture.Name),key,arguments);
        }
        public string TextIn(string locale,string key,params object[] arguments) {
            string value;var language=LanguageFor(locale);
            if(!catalogs[language].TryGetValue(key,out value) && !catalogs["en-US"].TryGetValue(key,out value))value=key;
            for(int i=arguments.Length;i>0;--i)value=value.Replace("%"+i,Convert.ToString(arguments[i-1],CultureInfo.InvariantCulture));
            return value;
        }
    }
}
