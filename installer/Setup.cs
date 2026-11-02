// SPDX-License-Identifier: GPL-2.0-or-later
// Contract/resource inspection follows the NEXUS Dynamic Stream Title installer.
using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Drawing;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Web.Script.Serialization;
using System.Windows.Forms;
[assembly: AssemblyTitle("OBS News Ticker NEXUS Setup")]
[assembly: AssemblyProduct("OBS News Ticker NEXUS")]
[assembly: AssemblyCompany("OBS NEXUS / DjEmiMas")]
[assembly: AssemblyVersion(Payload.Version+".0")]
[assembly: AssemblyFileVersion(Payload.Version+".0")]
[assembly: AssemblyInformationalVersion(Payload.Version)]
class Contract { public int Schema,Abi;public string Kind,Consumer,MinVersion,MaxVersionExclusive;public string Version=null; }
class SharedRecord { public int Abi;public string Version,Hash;public bool OwnedByTitle=false;public List<string> Consumers=new List<string>();public List<Contract> Requirements=new List<Contract>(); }
class InstallRecord { public string Version;public Dictionary<string,string> Files=new Dictionary<string,string>(); }
internal sealed class NexusForm:Form {internal bool PreviewOnly;protected override bool ShowWithoutActivation {get{return PreviewOnly;}}}
static class Setup {
 const string Id="obs-news-ticker-nexus",Plugin="obs-plugins/64bit/obs-news-ticker-nexus.dll",Core="bin/64bit/nexus-twitch-core.dll",Shared="data/nexus-twitch-core/runtime.json",Manifest="data/obs-plugins/obs-news-ticker-nexus/install.json";
 static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
 static bool fixture;
 static readonly Dictionary<string,string> Files=new Dictionary<string,string>{
  {Plugin,"plugin"},{"data/obs-plugins/obs-news-ticker-nexus/locale/es-ES.ini","es"},
  {"data/obs-plugins/obs-news-ticker-nexus/locale/en-US.ini","en"},
  {"data/obs-plugins/obs-news-ticker-nexus/locale/it-IT.ini","it"},
  {"data/obs-plugins/obs-news-ticker-nexus/locale/fr-FR.ini","fr"},
  {"data/obs-plugins/obs-news-ticker-nexus/locale/de-DE.ini","de"},
  {"data/obs-plugins/obs-news-ticker-nexus/effects/ticker-fade.effect","effect"},
  {"data/obs-plugins/obs-news-ticker-nexus/graphics/broadcast-reference.png","map-artwork"},
  {"data/obs-plugins/obs-news-ticker-nexus/graphics/broadcast-designs.png","design-artwork"},
  {"data/obs-plugins/obs-news-ticker-nexus/graphics/compact-reference.png","compact-artwork"},
  {"data/obs-plugins/obs-news-ticker-nexus/graphics/README.md","graphics-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/README.md","readme"},
  {"data/obs-plugins/obs-news-ticker-nexus/Twitch-Core.md","core-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/Broadcast-Reference.md","broadcast-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/Now-Playing-Setup.md","now-playing-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/Weather.md","weather-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/Hype-Train.md","hype-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/docs/twitch-core.md","core-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/docs/broadcast-reference.md","broadcast-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/docs/now-playing-setup.md","now-playing-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/docs/now-playing-json-v1.md","json-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/docs/weather.md","weather-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/docs/hype-train.md","hype-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/docs/RELEASE-1.0.0.md","release-guide"},
  {"data/obs-plugins/obs-news-ticker-nexus/core/PROVENANCE.md","core-provenance"},
  {"data/obs-plugins/obs-news-ticker-nexus/weather-capitals.json","weather-capitals"},
  {"data/obs-plugins/obs-news-ticker-nexus/LICENSE","license"}};
 [DllImport("kernel32",CharSet=CharSet.Unicode)]static extern IntPtr LoadLibraryEx(string path,IntPtr file,uint flags);
 [DllImport("kernel32",CharSet=CharSet.Unicode)]static extern IntPtr FindResource(IntPtr module,string name,IntPtr type);
 [DllImport("kernel32")]static extern uint SizeofResource(IntPtr module,IntPtr resource);
 [DllImport("kernel32")]static extern IntPtr LoadResource(IntPtr module,IntPtr resource);
 [DllImport("kernel32")]static extern IntPtr LockResource(IntPtr resource);
 [DllImport("kernel32")]static extern bool FreeLibrary(IntPtr module);
 static string PathIn(string root,string relative){
  root=Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar)+Path.DirectorySeparatorChar;
  string path=Path.GetFullPath(Path.Combine(root,relative.Replace('/',Path.DirectorySeparatorChar)));
  if(!path.StartsWith(root,StringComparison.OrdinalIgnoreCase))throw new IOException(L.T("Ruta fuera de OBS."));
  for(string parent=Path.GetDirectoryName(path);parent!=null;parent=Path.GetDirectoryName(parent))
   if(Directory.Exists(parent)&&(File.GetAttributes(parent)&FileAttributes.ReparsePoint)!=0)throw new IOException(L.T("No se admiten enlaces de directorios."));
  if(File.Exists(path)&&(File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new IOException(L.T("No se admiten enlaces de archivos."));
  return path;
 }
 static byte[] Resource(string name){using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)){if(stream==null)throw new IOException(L.T("Falta un recurso del paquete."));using(var copy=new MemoryStream()){stream.CopyTo(copy);return copy.ToArray();}}}
 static string Hash(byte[] bytes){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();}
 static string FileHash(string path){return File.Exists(path)?Hash(File.ReadAllBytes(path)):null;}
 static byte[] Encode(object value){return Encoding.UTF8.GetBytes(Json.Serialize(value));}
 static T Read<T>(string path) where T:class {if(!File.Exists(path))return null;if(new FileInfo(path).Length>65536)throw new IOException(L.T("Manifiesto demasiado grande."));var value=Json.Deserialize<T>(File.ReadAllText(path));if(value==null)throw new IOException(L.T("Manifiesto inválido."));return value;}
 static Version VersionOf(string text){Version value;if(text==null||!System.Text.RegularExpressions.Regex.IsMatch(text,@"^\d+\.\d+\.\d+$")||!Version.TryParse(text,out value))throw new IOException(L.T("Versión inválida."));return value;}
 static Contract Validate(Contract value,string kind){if(value==null||value.Schema!=1||value.Kind!=kind||value.Abi!=6)throw new IOException(L.T("Contrato de Twitch Core desconocido o incompatible."));if(kind=="core")VersionOf(value.Version);else if(String.IsNullOrWhiteSpace(value.Consumer)||VersionOf(value.MinVersion)>=VersionOf(value.MaxVersionExclusive))throw new IOException(L.T("Contrato de consumidor inválido."));return value;}
 static Contract DllContract(string path){
  IntPtr module=LoadLibraryEx(Path.GetFullPath(path),IntPtr.Zero,0x22);if(module==IntPtr.Zero)return null;
  try{IntPtr resource=FindResource(module,"NEXUS_TWITCH_CONTRACT",new IntPtr(10));if(resource==IntPtr.Zero)return null;uint size=SizeofResource(module,resource);if(size==0||size>65536)throw new IOException(L.T("Contrato inválido."));IntPtr pointer=LockResource(LoadResource(module,resource));if(pointer==IntPtr.Zero)throw new IOException(L.T("Contrato ilegible."));byte[] data=new byte[size];Marshal.Copy(pointer,data,0,(int)size);return Json.Deserialize<Contract>(Encoding.UTF8.GetString(data));}finally{FreeLibrary(module);}
 }
 static void Accepts(Contract consumer,Contract core){Validate(consumer,"consumer");Validate(core,"core");if(consumer.Abi!=core.Abi||VersionOf(core.Version)<VersionOf(consumer.MinVersion)||VersionOf(core.Version)>=VersionOf(consumer.MaxVersionExclusive))throw new IOException(L.T("Core incompatible con ")+consumer.Consumer+L.T(". No se ha actualizado."));}
 static void Atomic(string path,byte[] data,bool backup){
  Directory.CreateDirectory(Path.GetDirectoryName(path));if(FileHash(path)==Hash(data))return;
  string temp=path+".nexus-new-"+Guid.NewGuid().ToString("N");
  try{File.WriteAllBytes(temp,data);if(File.Exists(path))File.Replace(temp,path,backup?path+".nexus-backup-"+DateTime.UtcNow.ToString("yyyyMMddHHmmssfff")+"-"+Guid.NewGuid().ToString("N"):null);else File.Move(temp,path);}finally{if(File.Exists(temp))File.Delete(temp);}
 }
 static void Transaction(Dictionary<string,byte[]> writes){
  var old=new Dictionary<string,byte[]>();foreach(string path in writes.Keys)old[path]=File.Exists(path)?File.ReadAllBytes(path):null;
  var completed=new List<string>();try{foreach(var pair in writes){Atomic(pair.Key,pair.Value,true);completed.Add(pair.Key);}}
  catch{foreach(string path in completed.AsEnumerable().Reverse()){if(old[path]==null){if(File.Exists(path))File.Delete(path);}else Atomic(path,old[path],false);}throw;}
 }
 static void RequireClosed(bool running){if(running)throw new IOException(L.T("Cierra OBS antes de instalar o desinstalar."));}
 static void ValidateObs(string root){
  string exe=PathIn(root,"bin/64bit/obs64.exe");if(!File.Exists(exe))throw new IOException(L.T("Selecciona la raíz de OBS que contiene bin/64bit/obs64.exe."));
  if(!(FileVersionInfo.GetVersionInfo(exe).ProductVersion??"").StartsWith("32."))throw new IOException(L.T("Se necesita OBS 32.x de Windows x64."));
  using(var reader=new BinaryReader(File.OpenRead(exe))){if(reader.ReadUInt16()!=0x5a4d)throw new IOException(L.T("Ejecutable OBS inválido."));reader.BaseStream.Position=0x3c;reader.BaseStream.Position=reader.ReadInt32();if(reader.ReadUInt32()!=0x4550||reader.ReadUInt16()!=0x8664)throw new IOException(L.T("OBS debe ser x64."));}
 }
 static bool Portable(string root){return new[]{"portable_mode","portable_mode.txt","obs_portable_mode","obs_portable_mode.txt"}.Any(name=>File.Exists(Path.Combine(root,name)));}
 static IEnumerable<string> Plugins(string root){
  var folders=new List<string>{PathIn(root,"obs-plugins/64bit")};
  if(!fixture&&!Portable(root))foreach(string basePath in new[]{Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData),Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData)}){
   string folder=Path.Combine(basePath,"obs-studio","plugins");if(!Directory.Exists(folder))continue;folders.Add(folder);foreach(string child in Directory.GetDirectories(folder))folders.Add(Path.Combine(child,"bin","64bit"));
  }
  return folders.Where(Directory.Exists).SelectMany(folder=>Directory.GetFiles(folder,"*.dll")).Distinct(StringComparer.OrdinalIgnoreCase);
 }
 static void Install(string root){
  ValidateObs(root);var prior=Read<InstallRecord>(PathIn(root,Manifest));if(prior!=null&&(prior.Files==null||VersionOf(prior.Version)>VersionOf(Payload.Version)))throw new IOException(L.T("Existe una versión posterior o un manifiesto inválido."));
  var shared=Read<SharedRecord>(PathIn(root,Shared));if(shared!=null&&(shared.Consumers==null||shared.Requirements==null||shared.Hash==null||shared.Abi!=6))throw new IOException(L.T("Registro compartido inválido."));
  foreach(var hash in Payload.Hashes)if(Hash(Resource(hash.Key))!=hash.Value)throw new IOException(L.T("Paquete dañado."));
  var packed=Validate(Json.Deserialize<Contract>(Encoding.UTF8.GetString(Resource("core-contract"))),"core");
  var own=Validate(Json.Deserialize<Contract>(Encoding.UTF8.GetString(Resource("ticker-contract"))),"consumer");
  var requirements=new List<Contract>{own};
  foreach(string path in Plugins(root)){
   string name=Path.GetFileName(path);if(name.Equals(Id+".dll",StringComparison.OrdinalIgnoreCase))continue;
   var requirement=DllContract(path);if(requirement!=null){requirements.Add(Validate(requirement,"consumer"));continue;}
   if(name.Equals("obs-twitch-overlays.dll",StringComparison.OrdinalIgnoreCase)||name.Equals("obs-dynamic-stream-title.dll",StringComparison.OrdinalIgnoreCase)||new FileInfo(path).Length<100*1024*1024&&Encoding.ASCII.GetString(File.ReadAllBytes(path)).IndexOf("nexus-twitch-core.dll",StringComparison.OrdinalIgnoreCase)>=0)throw new IOException(L.T("No se puede comprobar la compatibilidad de ")+name+". Actualízalo primero.");
  }
  if(shared!=null)foreach(string consumer in shared.Consumers.Where(c=>c!=Id))if(!requirements.Any(r=>r.Consumer==consumer)){
   var requirement=shared.Requirements.FirstOrDefault(r=>r.Consumer==consumer);if(requirement==null)throw new IOException(L.T("Falta el contrato de ")+consumer);requirements.Add(Validate(requirement,"consumer"));
  }
  string corePath=PathIn(root,Core),current=FileHash(corePath);Contract selected=packed;bool replace=current==null;
  if(current!=null){
   var installed=Validate(DllContract(corePath),"core");
   if(shared!=null&&shared.Hash!=current)throw new IOException(L.T("El Core compartido cambió fuera del instalador. Se conserva."));
   if(current==Payload.Hashes["core"]||VersionOf(installed.Version)>VersionOf(packed.Version))selected=installed;
   else if(VersionOf(installed.Version)==VersionOf(packed.Version))throw new IOException(L.T("Misma versión de Core con contenido diferente. Se conserva."));
   else{if(shared==null&&current!=Payload.PreviousCoreHash&&current!=Payload.PreviousTickerCoreHash&&current!=Payload.PreviousHypeCoreHash)throw new IOException(L.T("Core anterior no identificado; se conserva."));replace=true;}
  }
  foreach(var requirement in requirements)Accepts(requirement,selected);
  if(shared==null)shared=new SharedRecord();shared.Abi=selected.Abi;shared.Version=selected.Version;shared.Hash=replace?Payload.Hashes["core"]:current;
  shared.Requirements=requirements;shared.Consumers=requirements.Select(r=>r.Consumer).Distinct().ToList();
  var writes=new Dictionary<string,byte[]>();var record=new InstallRecord{Version=Payload.Version};
  foreach(var pair in Files){writes[PathIn(root,pair.Key)]=Resource(pair.Value);record.Files[pair.Key]=Payload.Hashes[pair.Value];}
  if(replace)writes[corePath]=Resource("core");writes[PathIn(root,Shared)]=Encode(shared);writes[PathIn(root,Manifest)]=Encode(record);
  Transaction(writes);
 }
 static void Uninstall(string root){
  ValidateObs(root);var record=Read<InstallRecord>(PathIn(root,Manifest));if(record==null)return;if(record.Files==null)throw new IOException(L.T("Manifiesto inválido."));
  string plugin=PathIn(root,Plugin);if(File.Exists(plugin)&&(!record.Files.ContainsKey(Plugin)||FileHash(plugin)!=record.Files[Plugin]))throw new IOException(L.T("El plugin fue sustituido. Se conserva."));
  var shared=Read<SharedRecord>(PathIn(root,Shared));if(shared!=null){if(shared.Consumers==null||shared.Requirements==null)throw new IOException(L.T("Registro compartido inválido."));shared.Consumers.RemoveAll(c=>c==Id);shared.Requirements.RemoveAll(c=>c.Consumer==Id);Atomic(PathIn(root,Shared),Encode(shared),true);}
  foreach(string relative in Files.Keys){string path=PathIn(root,relative);if(record.Files.ContainsKey(relative)&&FileHash(path)==record.Files[relative])File.Delete(path);}
  File.Delete(PathIn(root,Manifest));
  // Core, credentials, history, backups and modified/unrelated files are retained.
 }
 static void Check(bool ok,string name){if(!ok)throw new Exception("FAIL: "+name);Console.WriteLine("PASS "+name);}
 static bool Refused(Action operation){try{operation();return false;}catch{return true;}}
 static void SelfTest(string folder,string obs,string log,string previous){
  if(Directory.Exists(folder))throw new IOException(L.T("La carpeta de prueba debe ser nueva."));fixture=true;Directory.CreateDirectory(folder);
  string root=Path.Combine(folder,"OBS á ñ");Directory.CreateDirectory(Path.GetDirectoryName(PathIn(root,"bin/64bit/obs64.exe")));File.Copy(obs,PathIn(root,"bin/64bit/obs64.exe"));File.WriteAllText(Path.Combine(root,"portable_mode.txt"),"");
  var lines=new List<string>();Action<bool,string> test=(ok,name)=>{Check(ok,name);lines.Add("PASS "+name);};
  int priorLanguage=L.Language;var translatedButton=new Button{Text="Instalar / actualizar"};
  string[] captions={"Instalar / actualizar","Install / update","Installa / aggiorna","Installer / mettre à jour","Installieren / aktualisieren"};
  for(int language=0;language<5;language++){L.Language=language;L.Apply(translatedButton);test(translatedButton.Text==captions[language],"installer language "+L.Codes[language]);}
  translatedButton.Dispose();L.Language=priorLanguage;
  test(Refused(()=>RequireClosed(true)),"running OBS guard");test(Refused(()=>ValidateObs(Path.Combine(folder,"invalid"))),"invalid OBS root");
  test(Refused(()=>PathIn(root,"../../escape")),"path traversal rejected");
  Install(root);test(FileHash(PathIn(root,Plugin))==Payload.Hashes["plugin"]&&FileHash(PathIn(root,Core))==Payload.Hashes["core"],"fresh Unicode install includes Core");
  test(new[]{"es-ES","en-US","it-IT","fr-FR","de-DE"}.All(locale=>File.Exists(PathIn(root,"data/obs-plugins/obs-news-ticker-nexus/locale/"+locale+".ini"))),"all five settings languages installed");
  test(DllContract(PathIn(root,Plugin)).Consumer==Id&&DllContract(PathIn(root,Core)).Version=="1.3.1","embedded contracts inspect without execution");
  Install(root);test(Read<SharedRecord>(PathIn(root,Shared)).Consumers.Count(c=>c==Id)==1,"idempotent reinstall and shared registration");
  File.WriteAllText(PathIn(root,"data/obs-plugins/obs-news-ticker-nexus/locale/es-ES.ini"),"changed");Install(root);test(Directory.GetFiles(Path.GetDirectoryName(PathIn(root,"data/obs-plugins/obs-news-ticker-nexus/locale/es-ES.ini")),"*.nexus-backup-*").Length>0,"replacement backup");
  string unrelated=PathIn(root,"obs-plugins/64bit/unrelated.dll");File.WriteAllText(unrelated,"keep");string config=PathIn(root,"config/obs-studio/plugin_config/nexus-twitch-core/user.txt");Atomic(config,Encoding.UTF8.GetBytes("keep-state"),false);
  var shared=Read<SharedRecord>(PathIn(root,Shared));shared.Consumers.Add("other");shared.Requirements.Add(new Contract{Schema=1,Kind="consumer",Consumer="other",Abi=6,MinVersion="1.0.0",MaxVersionExclusive="2.0.0"});Atomic(PathIn(root,Shared),Encode(shared),false);
  Uninstall(root);Uninstall(root);test(!File.Exists(PathIn(root,Plugin))&&File.Exists(PathIn(root,Core))&&File.ReadAllText(unrelated)=="keep"&&File.ReadAllText(config)=="keep-state","uninstall preserves shared Core, other data and credentials");
  Install(root);string before=FileHash(PathIn(root,Plugin));Atomic(PathIn(root,Shared),Encoding.UTF8.GetBytes("invalid"),false);test(Refused(()=>Install(root))&&FileHash(PathIn(root,Plugin))==before,"invalid registry rejected before writes");Atomic(PathIn(root,Shared),Encode(shared),false);
  shared.Requirements.First(r=>r.Consumer=="other").MaxVersionExclusive="1.1.0";Atomic(PathIn(root,Shared),Encode(shared),false);test(Refused(()=>Install(root)),"incompatible registered consumer blocks Core");shared.Requirements.First(r=>r.Consumer=="other").MaxVersionExclusive="2.0.0";Atomic(PathIn(root,Shared),Encode(shared),false);
  Atomic(PathIn(root,Core),Encoding.UTF8.GetBytes("unknown-core"),false);test(Refused(()=>Install(root))&&File.ReadAllText(PathIn(root,Core))=="unknown-core","unknown Core preserved");Atomic(PathIn(root,Core),Resource("core"),false);
  string scene=PathIn(root,"config/obs-studio/basic/scenes/Ticker.json");Atomic(scene,Encoding.UTF8.GetBytes("{\"sources\":[{\"id\":\"obs_news_ticker_nexus\",\"settings\":{\"header_text\":\"MY CHANNEL\"}}]}"),false);string sceneHash=FileHash(scene);
  var record=Read<InstallRecord>(PathIn(root,Manifest));record.Version="0.7.1";Atomic(PathIn(root,Manifest),Encode(record),false);Install(root);test(Read<InstallRecord>(PathIn(root,Manifest)).Version==Payload.Version&&FileHash(scene)==sceneHash&&File.ReadAllText(config)=="keep-state","development upgrade preserves scene settings and credentials");
  record=Read<InstallRecord>(PathIn(root,Manifest));record.Version="99.0.0";Atomic(PathIn(root,Manifest),Encode(record),false);test(Refused(()=>Install(root)),"plugin downgrade rejected");record.Version=Payload.Version;Atomic(PathIn(root,Manifest),Encode(record),false);
  if(previous!=null){
   string previousHash=FileHash(previous);test(previousHash==Payload.PreviousCoreHash||previousHash==Payload.PreviousTickerCoreHash||previousHash==Payload.PreviousHypeCoreHash,"recognized previous Core fixture");
   Atomic(PathIn(root,Core),File.ReadAllBytes(previous),false);shared.Hash=previousHash;shared.Version=DllContract(previous).Version;Atomic(PathIn(root,Shared),Encode(shared),false);
   Install(root);test(FileHash(PathIn(root,Core))==Payload.Hashes["core"],"compatible previous Core upgraded");
   test(Directory.GetFiles(Path.GetDirectoryName(PathIn(root,Core)),"*.nexus-backup-*").Any(path=>FileHash(path)==previousHash),"previous Core backup retained");
   string current=FileHash(PathIn(root,Core));Install(root);test(FileHash(PathIn(root,Core))==current,"compatible existing Core reused");
   if(previousHash==Payload.PreviousTickerCoreHash||previousHash==Payload.PreviousHypeCoreHash){
    string legacyRoot=Path.Combine(folder,"OBS120");Directory.CreateDirectory(Path.GetDirectoryName(PathIn(legacyRoot,"bin/64bit/obs64.exe")));File.Copy(obs,PathIn(legacyRoot,"bin/64bit/obs64.exe"));File.WriteAllText(Path.Combine(legacyRoot,"portable_mode.txt"),"");
    Atomic(PathIn(legacyRoot,Core),File.ReadAllBytes(previous),false);Install(legacyRoot);test(FileHash(PathIn(legacyRoot,Core))==Payload.Hashes["core"],"recognized previous Core without registry upgrades safely");
   }
  }
  File.WriteAllText(PathIn(root,Plugin),"modified");test(Refused(()=>Uninstall(root))&&File.ReadAllText(PathIn(root,Plugin))=="modified","modified plugin not deleted");
  File.WriteAllText(log,String.Join(Environment.NewLine,lines)+"\nRESULT checks="+lines.Count+" failures=0\n");
 }
 static Form CreateForm(bool preview=false){
  var form=new NexusForm{PreviewOnly=preview,Text="OBS News Ticker NEXUS — "+Payload.Version,ClientSize=new Size(920,500),AutoScaleMode=AutoScaleMode.Dpi,StartPosition=FormStartPosition.CenterScreen,Font=new Font("Segoe UI",9),BackColor=Color.FromArgb(20,22,34),ForeColor=Color.White,FormBorderStyle=FormBorderStyle.FixedDialog,MaximizeBox=false};
  using(var stream=new MemoryStream(Resource("app-icon")))form.Icon=new Icon(stream);
  var artwork=new PictureBox{Left=0,Top=0,Width=333,Height=500,SizeMode=PictureBoxSizeMode.Zoom,BackColor=Color.FromArgb(5,12,18)};
  using(var stream=new MemoryStream(Resource("branding")))using(var image=Image.FromStream(stream))artwork.Image=new Bitmap(image);
  form.Controls.Add(artwork);form.Disposed+=(s,e)=>artwork.Image.Dispose();
  form.Controls.Add(new Label{Text="OBS News Ticker NEXUS",Left=360,Top=25,Width=530,Height=45,Font=new Font("Segoe UI",21,FontStyle.Bold)});
  form.Controls.Add(new Label{Text=Payload.Version,Left=362,Top=76,Width=525,Height=24,ForeColor=Color.FromArgb(170,178,199),Font=new Font("Segoe UI",11)});
  form.Controls.Add(new Label{Text="Idioma",Left=362,Top=116,Width=90,Height=24});
  var language=new ComboBox{Left=460,Top=112,Width=230,DropDownStyle=ComboBoxStyle.DropDownList,BackColor=Color.FromArgb(37,42,62),ForeColor=Color.White,FlatStyle=FlatStyle.Flat};language.Items.AddRange(L.Names);language.SelectedIndex=L.Language;form.Controls.Add(language);
  form.Controls.Add(new Label{Text="OBS 32.x · Windows x64 · Twitch Core Shared 1.3.1 incluido\nCierra OBS. Se conservarán tus escenas, credenciales e historial.",Left=362,Top=160,Width=525,Height=56});
  form.Controls.Add(new Label{Text="17 estilos · Música · Twitch · World Weather\nBreaking News de Hype Train con ancho y altura ajustables.\nNow Playing automático requiere Playing NEXUS instalado.",Left=362,Top=235,Width=525,Height=70,ForeColor=Color.FromArgb(190,197,215)});
  form.Controls.Add(new Label{Text="Carpeta de OBS Studio",Left=362,Top=322,Width=525,Height=22});
  var folder=new TextBox{Text=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),"obs-studio"),Left=362,Top=350,Width=410,BackColor=Color.FromArgb(30,34,48),ForeColor=Color.White,BorderStyle=BorderStyle.FixedSingle};form.Controls.Add(folder);
  var browse=new Button{Text="Carpeta…",Left=784,Top=346,Width=106,Height=30,FlatStyle=FlatStyle.Flat,BackColor=Color.FromArgb(37,42,62)};browse.Click+=(s,e)=>{using(var dialog=new FolderBrowserDialog{SelectedPath=folder.Text,Description=L.T("Carpeta de OBS Studio"),ShowNewFolderButton=false})if(dialog.ShowDialog()==DialogResult.OK)folder.Text=dialog.SelectedPath;};form.Controls.Add(browse);
  var actions=new List<Button>();
  foreach(bool install in new[]{true,false}){bool chosen=install;var button=new Button{Text=install?"Instalar / actualizar":"Desinstalar",Left=install?362:636,Top=422,Width=254,Height=45,FlatStyle=FlatStyle.Flat,BackColor=install?Color.FromArgb(103,65,187):Color.FromArgb(37,42,62),ForeColor=Color.White};button.Click+=(s,e)=>{try{form.UseWaitCursor=true;foreach(var action in actions)action.Enabled=false;RequireClosed(Process.GetProcessesByName("obs64").Length!=0);if(chosen)Install(folder.Text);else Uninstall(folder.Text);MessageBox.Show(form,L.T("Operación completada. Configuración conservada."),"NEXUS");}catch(Exception error){MessageBox.Show(form,error.Message,"NEXUS",MessageBoxButtons.OK,MessageBoxIcon.Warning);}finally{form.UseWaitCursor=false;foreach(var action in actions)action.Enabled=true;}};actions.Add(button);form.Controls.Add(button);}
  var credit=new LinkLabel{Text="a plugin from DjEmiMas",Left=362,Top=480,Width=525,Height=20,LinkColor=Color.FromArgb(153,193,255),ActiveLinkColor=Color.Cyan};credit.Links.Add(14,8,"https://discord.gg/dhcqZPDYh");credit.LinkClicked+=(s,e)=>Process.Start(new ProcessStartInfo((string)e.Link.LinkData){UseShellExecute=true});form.Controls.Add(credit);
  language.SelectedIndexChanged+=(s,e)=>{L.Language=language.SelectedIndex;L.Apply(form);};L.Apply(form);return form;
 }
 [STAThread]static int Main(string[] args){
  try{
   L.Verify();
   if(args.Length==3&&args[0]=="/preview"){
    L.Language=Array.IndexOf(L.Codes,args[1]);if(L.Language<0)L.Language=1;Application.EnableVisualStyles();
    using(var form=CreateForm(true)){form.Opacity=0;form.ShowInTaskbar=false;form.StartPosition=FormStartPosition.Manual;form.Location=new Point(-32000,-32000);form.Show();Application.DoEvents();form.PerformLayout();using(var bitmap=new Bitmap(form.Width,form.Height)){form.DrawToBitmap(bitmap,new Rectangle(0,0,bitmap.Width,bitmap.Height));bitmap.Save(args[2],System.Drawing.Imaging.ImageFormat.Png);}}return 0;
   }
   if((args.Length==4||args.Length==5)&&args[0]=="/selftest"){SelfTest(args[1],args[2],args[3],args.Length==5?args[4]:null);return 0;}
   if(args.Length==2&&(args[0]=="/install"||args[0]=="/uninstall")){RequireClosed(Process.GetProcessesByName("obs64").Length!=0);if(args[0]=="/install")Install(args[1]);else Uninstall(args[1]);return 0;}
   Application.EnableVisualStyles();using(var form=CreateForm())Application.Run(form);return 0;
  }catch(Exception error){if((args.Length==4||args.Length==5)&&args[0]=="/selftest")File.WriteAllText(args[3],error.ToString());else if(args.Length>0&&args[0]=="/preview")Console.Error.WriteLine(error.ToString());else MessageBox.Show(error.Message,"OBS News Ticker NEXUS");return 1;}
 }
}
