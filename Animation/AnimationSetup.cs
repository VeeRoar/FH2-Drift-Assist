using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;

// Build with .NET Framework csc and references System.Windows.Forms and System.Web.Extensions.
internal static class AnimationSetup {
    static string Quote(string s) {
        if (s.Contains("\"")) throw new ArgumentException("Unexpected quote in argument.");
        string trimmed = s.TrimEnd('\\');
        return "\"" + trimmed + new string('\\', 2 * (s.Length - trimmed.Length)) + "\"";
    }

    static int Run(string file, IEnumerable<string> args) {
        var list = new List<string>();
        foreach (string arg in args) list.Add(Quote(arg));
        var psi = new ProcessStartInfo(file, string.Join(" ", list));
        psi.UseShellExecute = false;
        using (var p = Process.Start(psi)) { p.WaitForExit(); return p.ExitCode; }
    }

    static string Read(Dictionary<string, string> options, string key) {
        string value;
        return options.TryGetValue(key, out value) ? value : null;
    }

    static string Pick(string label, string pattern) {
        using (var dlg = new OpenFileDialog()) {
            dlg.Title = label;
            dlg.Filter = pattern + "|" + pattern;
            dlg.CheckFileExists = true;
            if (dlg.ShowDialog() != DialogResult.OK) throw new OperationCanceledException("Selection cancelled: " + label);
            return dlg.FileName;
        }
    }

    static string Runtime(string selected, bool needsNumpy) {
        string check = "import struct,sys" + (needsNumpy ? ",numpy" : "") +
            "; sys.exit(0 if sys.version_info >= (3,10) and struct.calcsize(chr(80)) == 8 else 1)";
        if (!string.IsNullOrEmpty(selected) && File.Exists(selected) && Run(selected, new [] {"-B", "-c", check}) == 0)
            return selected;
        var paths = new List<string>();
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        paths.Add(Environment.GetEnvironmentVariable("DRIFT_ANIMATION_PYTHON"));
        paths.Add(Path.Combine(local, @"Programs\Python\Python312\python.exe"));
        paths.Add(Path.Combine(local, @"Python\pythoncore-3.12-64\python.exe"));
        foreach (string raw in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(';')) {
            string dir = raw.Trim().Trim('"');
            if (dir.Length != 0 && dir.IndexOf(@"Microsoft\WindowsApps", StringComparison.OrdinalIgnoreCase) < 0)
                paths.Add(Path.Combine(dir, "python.exe"));
        }
        foreach (string p in paths) {
            if (File.Exists(p) && !String.Equals(p, selected, StringComparison.OrdinalIgnoreCase) &&
                Run(p, new [] {"-B", "-c", check}) == 0) return p;
        }
        if (needsNumpy) Console.WriteLine("Install NumPy in your Python 3.12 x64 installation with: py -3.12 -m pip install numpy");
        string path = Pick("Select 64-bit python.exe" + (needsNumpy ? " with NumPy / 選擇已安裝 NumPy 的 Python" : ""), "python.exe");
        if (Run(path, new [] {"-B", "-c", check}) != 0) throw new IOException("This Python installation cannot run the animation tools.");
        return path;
    }

    static string LocalFile(Dictionary<string,string> options, IDictionary<string,object> saved,
                            string option, string key, string title, string file, string adjacent) {
        string path = Read(options, option);
        if (!String.IsNullOrEmpty(path)) {
            if (!File.Exists(path)) throw new FileNotFoundException("Cannot find " + file, path);
            return Path.GetFullPath(path);
        }
        if (saved.ContainsKey(key) && File.Exists((string)saved[key])) return (string)saved[key];
        if (File.Exists(adjacent)) return adjacent;
        return Pick(title, file);
    }

    [STAThread]
    static int Main(string[] argv) {
        try {
            var opt = new Dictionary<string,string>(StringComparer.OrdinalIgnoreCase);
            bool uninstall = false;
            for (int i=0; i<argv.Length; i++) {
                string arg = argv[i].TrimStart('-','/');
                if (String.Equals(arg, "Uninstall", StringComparison.OrdinalIgnoreCase)) uninstall = true;
                else {
                    if (++i >= argv.Length) throw new ArgumentException("Missing value for " + arg);
                    opt.Add(arg, argv[i]);
                }
            }
            string game = opt["Game"].ToUpperInvariant();
            if (game != "FH2" && game != "FM6") throw new ArgumentException("Unknown game.");
            string az = Path.GetFullPath(opt["Azimuth"]);
            string mount = Path.GetFullPath(opt["Mount"]);
            string cfg = Path.Combine(az, "DriftAssistAnimation", game, "config.json");
            var saved = new Dictionary<string,object>();
            if (File.Exists(cfg)) saved = new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(cfg, Encoding.UTF8));
            else if (uninstall) { Console.WriteLine("No animation installation is registered."); return 0; }
            string python = Runtime(Read(opt,"Python") ?? (saved.ContainsKey("python") ? (string)saved["python"] : null), !uninstall);
            if (uninstall) return Run(python, new [] {"-B", (string)saved["manager"], "uninstall", "--config", cfg});
            string media = Read(opt,"FH6Media") ?? (saved.ContainsKey("fh6") ? (string)saved["fh6"] : null);
            if (String.IsNullOrEmpty(media) || !File.Exists(Path.Combine(media,"animation.zip")))
                media = Path.GetDirectoryName(Pick("Select animation.zip in FH6 Content/media / 選擇 FH6 動畫檔", "animation.zip"));
            string acl = LocalFile(opt,saved,"AclDll","acl","Select forzatech_acl.dll from Forza Mod Tool","forzatech_acl.dll",null);
            string granny = LocalFile(opt,saved,"GrannyDll","granny","Select granny2_x64.dll from ForzaTechStudio Native","granny2_x64.dll",null);
            string xcomp = LocalFile(opt,saved,"XcompressDll","xcompress","Select xcompress64.dll from ForzaTechStudio Native","xcompress64.dll",Path.Combine(Path.GetDirectoryName(granny),"xcompress64.dll"));
            return Run(python, new [] {"-B",Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"manager.py"),
                "setup","--game",game,"--azimuth",az,"--mount",mount,"--fh6",media,"--acl",acl,"--granny",granny,"--xcompress",xcomp});
        } catch (Exception e) {
            Console.Error.WriteLine("Animation setup: " + e.Message);
            return 1;
        }
    }
}
