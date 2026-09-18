// hash
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;













class Program
{
    const string DefaultDll =
        @"D:\Steam\steamapps\common\Red Dead Redemption 2\ScriptHookRDRNetAPI.dll";

    static Type[] SafeTypes(Assembly asm)
    {
        try { return asm.GetTypes(); }
        catch (ReflectionTypeLoadException ex)
        {
            var list = new List<Type>();
            foreach (var t in ex.Types) if (t != null) list.Add(t);
            Console.WriteLine("部分类型加载失败，已改用 " + list.Count + " 个已解析类型");
            return list.ToArray();
        }
    }

    
    static List<ulong> I64Constants(byte[] il)
    {
        var res = new List<ulong>();
        if (il == null) return res;
        for (int i = 0; i + 8 < il.Length; ++i)
        {
            if (il[i] != 0x21) continue;
            ulong v = 0;
            for (int k = 0; k < 8; ++k) v |= (ulong)il[i + 1 + k] << (8 * k);
            res.Add(v);
            i += 8;
        }
        return res;
    }

    static int DumpHashes(Assembly asm, string wantFile, string outFile)
    {
        var types = SafeTypes(asm);

        var wanted = new List<string>();
        if (File.Exists(wantFile))
            foreach (var ln in File.ReadAllLines(wantFile))
            {
                var s = ln.Trim();
                if (s.Length > 0 && !s.StartsWith("#")) wanted.Add(s);
            }
        if (wanted.Count == 0) { Console.WriteLine("待查列表为空: " + wantFile); return 2; }

        
        var index = new Dictionary<string, List<(string, MethodInfo)>>();
        foreach (var t in types)
        {
            if (t.FullName == null || !t.FullName.StartsWith("RDR2.Native")) continue;
            MethodInfo[] ms;
            try { ms = t.GetMethods(BindingFlags.Public | BindingFlags.Static); }
            catch { continue; }
            foreach (var m in ms)
            {
                if (!index.TryGetValue(m.Name, out var lst))
                {
                    lst = new List<(string, MethodInfo)>();
                    index[m.Name] = lst;
                }
                lst.Add((t.Name, m));
            }
        }

        int found = 0;
        using (var w = new StreamWriter(outFile))
        {
            foreach (var name in wanted)
            {
                string ns = null, mn = name;
                int dot = name.IndexOf('.');
                if (dot > 0) { ns = name.Substring(0, dot); mn = name.Substring(dot + 1); }

                if (!index.TryGetValue(mn, out var cands)) { Console.WriteLine("  未找到: " + name); continue; }
                foreach (var (tn, m) in cands)
                {
                    if (ns != null && !string.Equals(tn, ns, StringComparison.OrdinalIgnoreCase)) continue;
                    try
                    {
                        var il = m.GetMethodBody()?.GetILAsByteArray();
                        var c = I64Constants(il);
                        if (c.Count > 0)
                        {
                            w.WriteLine($"{tn}.{mn}=0x{c[0]:X16}");
                            Console.WriteLine($"  {tn}.{mn} = 0x{c[0]:X16}");
                            found++;
                        }
                        else Console.WriteLine($"  {tn}.{mn}: IL 里没有 ldc.i8（包装调用？）");
                    }
                    catch (Exception ex) { Console.WriteLine($"  {name}: 读 IL 失败 {ex.GetType().Name}"); }
                    break;
                }
            }
        }
        Console.WriteLine($"写出 {found} 条 -> {outFile}");
        return found > 0 ? 0 : 2;
    }

    static int DumpEnums(Assembly asm, string filter)
    {
        foreach (var t in SafeTypes(asm))
        {
            if (!t.IsEnum) continue;
            if (t.Name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) < 0) continue;
            Console.WriteLine("=== " + t.FullName);
            var names = Enum.GetNames(t);
            var values = Enum.GetValues(t);
            for (int i = 0; i < names.Length; ++i)
            {
                long sv = Convert.ToInt64(values.GetValue(i));
                ulong uv = unchecked((ulong)sv);
                Console.WriteLine($"   {sv,12}  0x{uv:X8}  {names[i]}");
            }
        }
        return 0;
    }

    static int Main(string[] args)
    {
        string mode = args.Length > 0 ? args[0] : "hashes";
        string dll = Environment.GetEnvironmentVariable("RDR2_API_DLL") ?? DefaultDll;

        Assembly asm;
        try { asm = Assembly.LoadFrom(dll); }
        catch (Exception ex) { Console.WriteLine("加载失败: " + ex.Message); return 1; }
        Console.WriteLine("程序集: " + asm.FullName);

        if (mode == "enums") return DumpEnums(asm, args.Length > 1 ? args[1] : "");
        return DumpHashes(asm,
                          args.Length > 1 ? args[1] : "natives_want.txt",
                          args.Length > 2 ? args[2] : "hashes.txt");
    }
}
