using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace UltraRender.GUI
{
    public class SceneObject
    {
        public string Command { get; set; } = "";
        public string Name { get; set; } = "";
        public string Type { get; set; } = "";
        
        // Transform Properties
        public bool HasTransform { get; set; } = false;
        public float[] Position { get; set; } = new float[3];
        public float[] Scale { get; set; } = new float[] { 1, 1, 1 };
        public float[] Rotation { get; set; } = new float[3];
        
        public List<string> Parameters { get; set; } = new List<string>(); // Extra params
        public string RawLine { get; set; } = "";

        public override string ToString()
        {
            if (Command == "resolution") return $"Resolution ({Position[0]}x{Position[1]})"; // Hack: use Pos X/Y for W/H
            if (!string.IsNullOrEmpty(Name)) return $"{Name} ({Type})";
            if (!string.IsNullOrEmpty(Type)) return Type;
            return Command;
        }

        public string ToScriptLine()
        {
            var sb = new StringBuilder();
            sb.Append(Command);
            
            if (Command == "add_entity")
            {
                // add_entity TYPE MATERIAL POS SCALE ROT EXTRAS
                sb.Append(" " + Type);
                sb.Append(" " + Name); // Material Name
                
                // Transform
                sb.Append($" {Position[0]} {Position[1]} {Position[2]}");
                sb.Append($" {Scale[0]} {Scale[1]} {Scale[2]}");
                
                // Only write rotation if it's non-zero or we originally had it? 
                // To be safe, always write it if HasTransform implies 9 params, 
                // but some entities only support 6. 
                // Let's assume if we parsed 9, we write 9.
                // Or better: check how many params we consumed into Transform.
                // For simplicity, let's write Rotation if it was read or if user modified it.
                // We'll rely on the parser logic to set a flag or just append all.
                
                // Actually, to preserve original format as much as possible:
                // If we extracted transform, we write it back.
                if (HasRotation)
                {
                    sb.Append($" {Rotation[0]} {Rotation[1]} {Rotation[2]}");
                }
                
                foreach (var p in Parameters) sb.Append(" " + p);
            }
            else if (Command == "camera")
            {
                 // camera pos X Y Z lookat X Y Z fov F
                 sb.Append(" pos");
                 sb.Append($" {Position[0]} {Position[1]} {Position[2]}");
                 
                 // LookAt is stored in Parameters[0..2]
                 if (Parameters.Count >= 3)
                 {
                     sb.Append(" lookat");
                     sb.Append($" {Parameters[0]} {Parameters[1]} {Parameters[2]}");
                 }
                 
                 // FOV is stored in Parameters[3]
                 if (Parameters.Count >= 4)
                 {
                     sb.Append(" fov");
                     sb.Append($" {Parameters[3]}");
                 }
            }
            else
            {
                if (!string.IsNullOrEmpty(Name)) sb.Append(" " + Name);
                if (!string.IsNullOrEmpty(Type)) sb.Append(" " + Type);
                foreach (var p in Parameters) sb.Append(" " + p);
            }
            
            return sb.ToString();
        }
        
        public bool HasRotation { get; set; } = false;
    }

    public class SceneData
    {
        public int Width { get; set; } = 1280;
        public int Height { get; set; } = 720;
        
        public List<SceneObject> AllObjects { get; set; } = new List<SceneObject>();

        public IEnumerable<SceneObject> Materials => AllObjects.Where(o => o.Command == "define_material");
        public IEnumerable<SceneObject> Entities => AllObjects.Where(o => o.Command == "add_entity");
        public SceneObject? Camera => AllObjects.FirstOrDefault(o => o.Command == "camera");
        public SceneObject? Resolution => AllObjects.FirstOrDefault(o => o.Command == "resolution");

        public void Save(string path)
        {
            var lines = new List<string>();
            foreach (var obj in AllObjects)
            {
                lines.Add(obj.ToScriptLine());
            }
            File.WriteAllLines(path, lines);
        }
    }

    public static class SceneParser
    {
        public static SceneData Parse(string path)
        {
            var data = new SceneData();
            if (!File.Exists(path)) return data;

            var lines = File.ReadAllLines(path);
            foreach (var line in lines)
            {
                var trimmed = line.Trim();
                if (string.IsNullOrEmpty(trimmed) || trimmed.StartsWith("#")) continue;

                var parts = trimmed.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length == 0) continue;

                string cmd = parts[0].ToLower();
                var obj = new SceneObject { Command = cmd, RawLine = line };

                if (cmd == "resolution" && parts.Length >= 3)
                {
                    if (int.TryParse(parts[1], out int w)) { data.Width = w; obj.Position[0] = w; }
                    if (int.TryParse(parts[2], out int h)) { data.Height = h; obj.Position[1] = h; }
                    obj.Parameters.Add(parts[1]); 
                    obj.Parameters.Add(parts[2]);
                }
                else if (cmd == "camera")
                {
                    obj.Type = "Perspective";
                    
                    // Format: camera pos <px> <py> <pz> lookat <lx> <ly> <lz> fov <f>
                    int currentIdx = 1;
                    
                    // Check for "pos" keyword (optional, sometimes implicit)
                    if (currentIdx < parts.Length && parts[currentIdx].ToLower() == "pos") currentIdx++;
                    
                    // Parse Position
                    if (currentIdx + 2 < parts.Length && IsFloat(parts[currentIdx]))
                    {
                        obj.Position[0] = float.Parse(parts[currentIdx]);
                        obj.Position[1] = float.Parse(parts[currentIdx+1]);
                        obj.Position[2] = float.Parse(parts[currentIdx+2]);
                        obj.HasTransform = true; // Mark as having position
                        currentIdx += 3;
                    }

                    // Check for "lookat" keyword
                    if (currentIdx < parts.Length && parts[currentIdx].ToLower() == "lookat") currentIdx++;

                    // Parse LookAt (stored in Parameters 0-2)
                    if (currentIdx + 2 < parts.Length && IsFloat(parts[currentIdx]))
                    {
                        obj.Parameters.Add(parts[currentIdx]);
                        obj.Parameters.Add(parts[currentIdx+1]);
                        obj.Parameters.Add(parts[currentIdx+2]);
                        currentIdx += 3;
                    }

                    // Check for "fov" keyword
                    if (currentIdx < parts.Length && parts[currentIdx].ToLower() == "fov") currentIdx++;

                    // Parse FOV (stored in Parameters 3)
                    if (currentIdx < parts.Length && IsFloat(parts[currentIdx]))
                    {
                        obj.Parameters.Add(parts[currentIdx]);
                        currentIdx++;
                    }
                }
                else if (cmd == "define_material" && parts.Length >= 3)
                {
                    obj.Name = parts[1];
                    obj.Type = parts[2];
                    obj.Parameters.AddRange(parts.Skip(3));
                }
                else if (cmd == "add_entity" && parts.Length >= 3)
                {
                    obj.Type = parts[1];
                    obj.Name = parts[2]; // Material Name
                    
                    // Parse Transform
                    // Expecting: [Type] [Mat] [Px Py Pz] [Sx Sy Sz] [Rx Ry Rz]? [Extras...]
                    // Indices: 0=cmd, 1=type, 2=mat, 3=Px
                    
                    int currentIdx = 3;
                    bool parsedTransform = false;
                    
                    // Try parse Position (3 floats)
                    if (parts.Length >= currentIdx + 3 && IsFloat(parts[currentIdx]))
                    {
                        obj.Position[0] = float.Parse(parts[currentIdx]);
                        obj.Position[1] = float.Parse(parts[currentIdx+1]);
                        obj.Position[2] = float.Parse(parts[currentIdx+2]);
                        currentIdx += 3;
                        parsedTransform = true;
                    }
                    
                    // Try parse Scale (3 floats)
                    if (parsedTransform && parts.Length >= currentIdx + 3 && IsFloat(parts[currentIdx]))
                    {
                        obj.Scale[0] = float.Parse(parts[currentIdx]);
                        obj.Scale[1] = float.Parse(parts[currentIdx+1]);
                        obj.Scale[2] = float.Parse(parts[currentIdx+2]);
                        currentIdx += 3;
                    }
                    
                    // Try parse Rotation (3 floats) - Optional
                    // Check if next 3 are floats
                    if (parsedTransform && parts.Length >= currentIdx + 3 && IsFloat(parts[currentIdx]) && IsFloat(parts[currentIdx+1]) && IsFloat(parts[currentIdx+2]))
                    {
                        obj.Rotation[0] = float.Parse(parts[currentIdx]);
                        obj.Rotation[1] = float.Parse(parts[currentIdx+1]);
                        obj.Rotation[2] = float.Parse(parts[currentIdx+2]);
                        currentIdx += 3;
                        obj.HasRotation = true;
                    }

                    if (parsedTransform)
                    {
                        obj.HasTransform = true;
                        // Add remaining as extras
                        if (currentIdx < parts.Length)
                        {
                            obj.Parameters.AddRange(parts.Skip(currentIdx));
                        }
                    }
                    else
                    {
                        // Fallback
                        obj.Parameters.AddRange(parts.Skip(3));
                    }
                }
                else
                {
                    obj.Parameters.AddRange(parts.Skip(1));
                }

                data.AllObjects.Add(obj);
            }

            return data;
        }

        private static bool IsFloat(string s) => float.TryParse(s, out _);
    }
}
