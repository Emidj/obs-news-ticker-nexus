// SPDX-License-Identifier: GPL-2.0-or-later
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Web.Script.Serialization;
using System.Windows.Forms;
internal static class L {
 internal static readonly string[] Codes={"es","en","it","fr","de"};
 internal static readonly string[] Names={"Español","English","Italiano","Français","Deutsch"};
 internal static int Language=Index(CultureInfo.CurrentUICulture.Name);
 static readonly Dictionary<string,string[]> Catalog=Load();
 static int Index(string name){int i=Array.IndexOf(Codes,(name??"").Split('-','_')[0].ToLowerInvariant());return i<0?1:i;}
 static Dictionary<string,string[]> Load(){using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("installer-locale"))using(var reader=new StreamReader(stream))return new JavaScriptSerializer().Deserialize<Dictionary<string,string[]>>(reader.ReadToEnd());}
 internal static string T(string value){string[] translations;return Catalog.TryGetValue(value,out translations)?translations[Language]:value;}
 internal static void Apply(Control root){
  string original=root.Tag as string;
  if(original==null&&Catalog.ContainsKey(root.Text)){original=root.Text;root.Tag=original;}
  if(original!=null)root.Text=T(original);
  foreach(Control child in root.Controls)Apply(child);
 }
 internal static void Verify(){foreach(var item in Catalog)if(item.Value.Length!=5||Array.Exists(item.Value,String.IsNullOrWhiteSpace))throw new InvalidDataException("Incomplete installer translation: "+item.Key);}
}
