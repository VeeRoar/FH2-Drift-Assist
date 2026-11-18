using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using System.Xml;

internal static class Installer {
    static byte[] Resource(string name) {
        using (var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name))
        using (var copy = new MemoryStream()) {
            stream.CopyTo(copy);
            return copy.ToArray();
        }
    }

    static string SelectFolder(string prompt) {
        using (var picker = new FolderBrowserDialog { Description = prompt, ShowNewFolderButton = false }) {
            if (picker.ShowDialog() != DialogResult.OK) throw new OperationCanceledException();
            return picker.SelectedPath;
        }
    }

    static bool IsMount(string path) {
        return File.Exists(Path.Combine(path, "AppxManifest.xml")) &&
               Directory.Exists(Path.Combine(path, "media", "stripped", "stringtables"));
    }

    static byte[] ReadTable(string mount, string lang) {
        foreach (string layer in new[] {"mediaoverride", "mediapc", "media"}) {
            string dir = Path.Combine(mount, layer, "stripped", "stringtables");
            string loose = Path.Combine(dir, lang, "Upgrades.str");
            if (File.Exists(loose)) return File.ReadAllBytes(loose);
            string archive = Path.Combine(dir, lang + ".zip");
            if (!File.Exists(archive)) continue;
            using (var zip = ZipFile.OpenRead(archive)) {
                var entry = zip.Entries.FirstOrDefault(e => String.Equals(e.FullName, "Upgrades.str", StringComparison.OrdinalIgnoreCase));
                if (entry == null) continue;
                using (var stream = entry.Open())
                using (var copy = new MemoryStream()) {
                    stream.CopyTo(copy);
                    return copy.ToArray();
                }
            }
        }
        throw new InvalidDataException("Missing garage table for " + lang);
    }

    sealed class Change {
        public string PathName, BackupName;
        public byte[] Before, After;
    }

    static byte[] Current(string file) { return File.Exists(file) ? File.ReadAllBytes(file) : null; }
    static bool Equal(byte[] a, byte[] b) { return a == null ? b == null : b != null && a.SequenceEqual(b); }

    static void Add(List<Change> changes, string root, string group, string relative, byte[] data) {
        string target = Path.Combine(root, relative);
        byte[] before = Current(target);
        if (!Equal(before, data))
            changes.Add(new Change {PathName=target, BackupName=Path.Combine(group, relative), Before=before, After=data});
    }

    static void Write(string path, byte[] data, bool exists) {
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        string tmp = path + ".drift-" + Guid.NewGuid().ToString("N") + ".tmp";
        try {
            File.WriteAllBytes(tmp, data);
            if (exists) File.Replace(tmp, path, null);
            else File.Move(tmp, path);
        } finally { if (File.Exists(tmp)) File.Delete(tmp); }
    }

    static void Install(string az, string mount, bool check) {
        if (!File.Exists(Path.Combine(az, "azimuth.exe"))) throw new IOException("Select the folder containing azimuth.exe.");
        if (!IsMount(mount) && IsMount(Path.Combine(mount, "Mount"))) mount = Path.Combine(mount, "Mount");
        if (!IsMount(mount)) throw new IOException("Select the game's Mount folder.");
        var manifest = new XmlDocument {XmlResolver=null};
        manifest.Load(Path.Combine(mount,"AppxManifest.xml"));
        var id = manifest.SelectSingleNode("/*/*[local-name()='Identity']") as XmlElement;
        if (id == null || id.GetAttribute("Name") != "Anthem" || id.GetAttribute("Version") != "1.0.0.64")
            throw new IOException("This release needs Forza Horizon 2 for Xbox One, build 1.0.0.64.");
        if (!check) {
            foreach (var process in Process.GetProcessesByName("azimuth")) using (process) {
                try {
                    if (String.Equals(process.MainModule.FileName, Path.Combine(az, "azimuth.exe"), StringComparison.OrdinalIgnoreCase))
                        throw new IOException("Close this Azimuth instance before installing.");
                } catch (InvalidOperationException) { /* Process exited during enumeration. */ }
            }
        }

        var changes = new List<Change>();
        var garage = new XmlDocument {XmlResolver=null};
        garage.LoadXml(Encoding.UTF8.GetString(Resource("garage")));
        string tables = Path.Combine(mount, "media", "stripped", "stringtables");
        string[] archives = Directory.GetFiles(tables, "*.zip");
        if (archives.Length == 0) throw new IOException("No language archives found.");
        foreach (string archive in archives) {
            string lang = Path.GetFileNameWithoutExtension(archive);
            string column = String.Equals(lang, "CHT", StringComparison.OrdinalIgnoreCase) ? "zh" : "en";
            var labels = garage.DocumentElement.ChildNodes.OfType<XmlElement>().ToArray();
            foreach (var label in labels) label.InnerText = label.GetAttribute(column);
            byte[] table = Lsb.Merge(ReadTable(mount, lang), labels);
            Add(changes, mount, "Mount", Path.Combine("mediaoverride","stripped","stringtables",lang,"Upgrades.str"), table);
        }
        string ini = Path.Combine("fh2-mods","FH2DriftAssist.ini");
        if (!File.Exists(Path.Combine(az, ini))) Add(changes, az, "Azimuth", ini, Resource("preset"));
        Add(changes, az, "Azimuth", Path.Combine("fh2-mods","FH2DriftAssist.dll"), Resource("plugin"));
        Console.WriteLine("{0} language tables checked. {1} files to install.", archives.Length, changes.Count);
        if (check || changes.Count == 0) return;

        string backup = Path.Combine(az, "ModBackups", "FH2-Drift-Assist-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0,8));
        Directory.CreateDirectory(backup);
        var report = new List<string> {"Azimuth = " + az, "Mount = " + mount};
        foreach (var change in changes) {
            if (change.Before != null) {
                string dest = Path.Combine(backup, change.BackupName);
                Directory.CreateDirectory(Path.GetDirectoryName(dest));
                File.WriteAllBytes(dest, change.Before);
            }
            report.Add((change.Before == null ? "NEW " : "REPLACED ") + change.BackupName);
        }
        File.WriteAllLines(Path.Combine(backup, "changes.txt"), report, Encoding.UTF8);
        Console.WriteLine("Backup: " + backup);
        var committed = new Stack<Change>();
        try {
            foreach (var change in changes) {
                if (!Equal(Current(change.PathName), change.Before)) throw new IOException("File changed during install: " + change.PathName);
                Write(change.PathName, change.After, change.Before != null);
                committed.Push(change);
                if (!Equal(Current(change.PathName), change.After)) throw new IOException("Write verification failed: " + change.PathName);
            }
        } catch {
            foreach (var change in committed) {
                if (change.Before == null) File.Delete(change.PathName);
                else Write(change.PathName, change.Before, true);
            }
            throw;
        }
        Console.WriteLine("Installed: drift assist + Formula Drift 60° + SLR 74°.");
    }

    [STAThread] static int Main(string[] args) {
        bool interactive = args.Length == 0;
        try {
            var paths = args.Where(x => x != "--check").ToArray();
            if (!interactive && paths.Length != 2) throw new ArgumentException("Install.exe [--check] AZIMUTH_FOLDER FH2_MOUNT");
            string az = Path.GetFullPath(interactive ? SelectFolder("Select the Azimuth folder (azimuth.exe) / 選擇 Azimuth 資料夾") : paths[0]);
            string mount = Path.GetFullPath(interactive ? SelectFolder("Select the Forza Horizon 2 Mount folder / 選擇 FH2 的 Mount 資料夾") : paths[1]);
            using (var mutex = new Mutex(false, @"Local\FH2DriftAssist051Installer")) {
                bool locked;
                try { locked = mutex.WaitOne(0); } catch (AbandonedMutexException) { locked = true; }
                if (!locked) throw new IOException("Another installation is running.");
                try { Install(az, mount, args.Contains("--check")); } finally { mutex.ReleaseMutex(); }
            }
            if (interactive) MessageBox.Show("Drift assist and both kits are installed. / 漂移輔助與兩套懸吊已安裝。");
            return 0;
        } catch (OperationCanceledException) { return 2; }
        catch (Exception ex) {
            Console.Error.WriteLine(ex.Message);
            if (interactive) MessageBox.Show(ex.Message, "FH2 Drift Assist");
            return 1;
        }
    }
}
