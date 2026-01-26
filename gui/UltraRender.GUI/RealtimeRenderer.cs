using OpenTK.Graphics.OpenGL;
using OpenTK.Mathematics;
using System;
using System.Collections.Generic;
using System.Linq;
using System.IO;

namespace UltraRender.GUI
{
    public class RealtimeRenderer
    {
        private int _shaderProgram;
        private int _gridShaderProgram; // New Shader for Grid
        private bool _initialized = false; // Initialization Flag

        public Action<string>? Logger; // Logger Delegate

        private Camera? _camera;
        private Matrix4 _projection;
        private List<RenderObject> _renderObjects = new List<RenderObject>();
        private SceneData? _lastScene;
        
        // Geometry Cache
        private Mesh? _cubeMesh;
        private Mesh? _sphereMesh;
        private Mesh? _quadMesh;
        private Mesh? _cylinderMesh; // Cylinder Mesh
        private Mesh? _torusMesh; // Torus Mesh
        private Mesh? _gridMesh; // Grid Mesh
        private Mesh? _axesMesh; // Axes Mesh
        
        // Cache for loaded meshes to avoid reloading same file
        private Dictionary<string, Mesh> _meshCache = new Dictionary<string, Mesh>();

        private long _frameCount = 0;

        public void Initialize()
        {
            if (_initialized) return;

            try 
            {
                GL.Enable(EnableCap.DepthTest);
                CheckError("Enable DepthTest");

                // Change background to a distinct color to verify GL is working
                GL.ClearColor(0.2f, 0.2f, 0.2f, 1.0f); 
                CheckError("ClearColor");
                
                CreateShaders();
                CreateGridShader(); // Compile Grid Shader
                InitializeGeometry();
                
                // Default View - Tilt down slightly to see grid
                _camera = new Camera(new Vector3(2, 3, 5), 1280f / 720f);
                _camera.Pitch = -25.0f; 
                _camera.Yaw = -135.0f; // Look at origin
                _camera.UpdateCameraVectors(); // Ensure vectors are updated

                _initialized = true;
                Logger?.Invoke("RealtimeRenderer Initialized Successfully.");
            }
            catch (Exception ex)
            {
                Logger?.Invoke($"RealtimeRenderer Init Fatal Error: {ex.Message}");
                throw;
            }
        }

        public void Resize(int width, int height)
        {
            try
            {
                if (!_initialized) return;
                if (width <= 0 || height <= 0) return; // Prevent invalid viewport

                GL.Viewport(0, 0, width, height);
                
                float aspect = (float)width / (float)height;
                if (float.IsNaN(aspect) || float.IsInfinity(aspect) || aspect <= 0) aspect = 1.0f;

                if (_camera != null)
                    _camera.AspectRatio = aspect;
                    
                _projection = Matrix4.CreatePerspectiveFieldOfView(MathHelper.DegreesToRadians(45f), aspect, 0.1f, 1000f);
            }
            catch (Exception ex)
            {
                Logger?.Invoke($"Resize Error: {ex.Message}");
            }
        }

        public void ResetScene()
        {
            _lastScene = null;
            _renderObjects.Clear();
            Logger?.Invoke("Realtime Renderer: Scene Reset.");
        }

        public void Render(SceneData? scene)
        {
            if (!_initialized) return;

            try
            {
                GL.Clear(ClearBufferMask.ColorBufferBit | ClearBufferMask.DepthBufferBit);
                
                // Draw Grid (Optional)
                DrawGrid();

                if (scene == null) return;

                // Sync if scene changed
                // Double check if ResetScene cleared _lastScene, this should force a re-sync
                SyncScene(scene);

                GL.UseProgram(_shaderProgram);
                
                int viewLoc = GL.GetUniformLocation(_shaderProgram, "view");
                int projLoc = GL.GetUniformLocation(_shaderProgram, "projection");
                int modelLoc = GL.GetUniformLocation(_shaderProgram, "model");
                int colorLoc = GL.GetUniformLocation(_shaderProgram, "objectColor");

                if (_camera != null)
                {
                    Matrix4 view = _camera.GetViewMatrix();
                    GL.UniformMatrix4(viewLoc, false, ref view);
                }
                GL.UniformMatrix4(projLoc, false, ref _projection);

                foreach (var obj in _renderObjects)
                {
                    if (obj.Mesh != null)
                    {
                        var model = obj.Transform;
                        GL.UniformMatrix4(modelLoc, false, ref model);
                        GL.Uniform3(colorLoc, obj.Color);
                        
                        obj.Mesh.Draw();
                    }
                }
                
                CheckError("Render Loop");
                
                _frameCount++;
            }
            catch (Exception ex)
            {
                // Throttle logging to avoid massive log files
                if (_frameCount % 60 == 0)
                {
                    Logger?.Invoke($"[CRITICAL] Render Loop Exception: {ex.Message} \n {ex.StackTrace}");
                }
            }
        }

        private void DrawGrid()
        {
            if (_gridMesh == null) return;
            
            GL.UseProgram(_gridShaderProgram);
            
            int viewLoc = GL.GetUniformLocation(_gridShaderProgram, "view");
            int projLoc = GL.GetUniformLocation(_gridShaderProgram, "projection");
            int modelLoc = GL.GetUniformLocation(_gridShaderProgram, "model");
            int colorLoc = GL.GetUniformLocation(_gridShaderProgram, "color");

            if (_camera != null)
            {
                Matrix4 view = _camera.GetViewMatrix();
                GL.UniformMatrix4(viewLoc, false, ref view);
            }
            
            Matrix4 model = Matrix4.Identity;
            Vector3 color = new Vector3(0.5f, 0.5f, 0.5f); // Lighter Gray Grid

            GL.UniformMatrix4(projLoc, false, ref _projection);
            GL.UniformMatrix4(modelLoc, false, ref model);
            GL.Uniform3(colorLoc, color);

            // Draw Lines
            GL.BindVertexArray(_gridMesh.Vao);
            GL.DrawElements(PrimitiveType.Lines, _gridMesh.Count, DrawElementsType.UnsignedInt, 0);
            GL.BindVertexArray(0);
        }

        private void DrawAxes()
        {
            if (_axesMesh == null) return;
            
            GL.UseProgram(_gridShaderProgram);
            GL.LineWidth(3.0f); // Make axes thicker
            
            int viewLoc = GL.GetUniformLocation(_gridShaderProgram, "view");
            int projLoc = GL.GetUniformLocation(_gridShaderProgram, "projection");
            int modelLoc = GL.GetUniformLocation(_gridShaderProgram, "model");
            int colorLoc = GL.GetUniformLocation(_gridShaderProgram, "color");

            if (_camera != null)
            {
                Matrix4 view = _camera.GetViewMatrix();
                GL.UniformMatrix4(viewLoc, false, ref view);
            }

            Matrix4 model = Matrix4.Identity;

            GL.UniformMatrix4(projLoc, false, ref _projection);
            GL.UniformMatrix4(modelLoc, false, ref model);

            GL.BindVertexArray(_axesMesh.Vao);

            // Draw X Axis (Red) - First 2 vertices
            GL.Uniform3(colorLoc, new Vector3(1.0f, 0.0f, 0.0f));
            GL.DrawElements(PrimitiveType.Lines, 2, DrawElementsType.UnsignedInt, 0); // Offset 0

            // Draw Y Axis (Green) - Next 2 vertices
            GL.Uniform3(colorLoc, new Vector3(0.0f, 1.0f, 0.0f));
            GL.DrawElements(PrimitiveType.Lines, 2, DrawElementsType.UnsignedInt, 2 * sizeof(uint)); // Offset 2 indices * 4 bytes

            // Draw Z Axis (Blue) - Last 2 vertices
            GL.Uniform3(colorLoc, new Vector3(0.0f, 0.0f, 1.0f));
            GL.DrawElements(PrimitiveType.Lines, 2, DrawElementsType.UnsignedInt, 4 * sizeof(uint)); // Offset 4 indices * 4 bytes

            GL.BindVertexArray(0);
            GL.LineWidth(1.0f); // Reset line width
        }
        
        private void CheckError(string stage)
        {
            ErrorCode err = GL.GetError();
            if (err != ErrorCode.NoError)
            {
                Logger?.Invoke($"OpenGL Error at {stage}: {err}");
            }
        }
        
        private void SyncScene(SceneData scene)
        {
            if (scene == null) return;
            // IMPORTANT: Only sync if the scene object reference has changed.
            // This prevents rebuilding the render list every frame (60fps * N objects = slow).
            if (_lastScene == scene) return; 
            _lastScene = scene;

            _renderObjects.Clear();
            
            // Camera Update - Only on initial load or if we want to force reset?
            // Let's only set if it's the first time or explicitly requested.
            // For now, we update if we haven't set up the camera properly or if it's a new scene.
            // To avoid overriding user movement every frame, we check if _lastScene changed.
            
            if (scene.Camera != null)
            {
                var p = scene.Camera.Parameters;
                Vector3 pos = new Vector3(0, 5, 10);
                Vector3 target = Vector3.Zero;
                float fov = 45f;
                
                for(int i=0; i<p.Count; i++)
                {
                    if (p[i] == "pos" && i+3 < p.Count)
                    {
                         pos = new Vector3(ParseFloat(p[i+1]), ParseFloat(p[i+2]), ParseFloat(p[i+3]));
                    }
                    else if (p[i] == "lookat" && i+3 < p.Count)
                    {
                         target = new Vector3(ParseFloat(p[i+1]), ParseFloat(p[i+2]), ParseFloat(p[i+3]));
                    }
                    else if (p[i] == "fov" && i+1 < p.Count)
                    {
                         fov = ParseFloat(p[i+1]);
                    }
                }
                // Update camera position and lookat
                if (_camera != null)
                {
                    _camera.Position = pos;
                    // Calculate Yaw/Pitch from LookAt
                    Vector3 diff = target - pos;
                    if (diff.LengthSquared < 0.0001f) diff = new Vector3(0, 0, -1);
                    Vector3 dir = Vector3.Normalize(diff);
                    if (float.IsNaN(dir.X) || float.IsNaN(dir.Y) || float.IsNaN(dir.Z)) dir = new Vector3(0, 0, -1);
                    
                    _camera.SetDirection(dir);
                    Logger?.Invoke($"Camera Updated: Pos={pos}, Dir={dir}");
                }
            }

            // Entities Update
            int count = 0;
            foreach (var entity in scene.Entities)
            {
                // ... (rest of entity loading logic)
                Mesh? mesh = null;
                string type = entity.Type.ToLower();
                
                if (type.Contains("sphere")) mesh = _sphereMesh;
                else if (type.Contains("cube") || type.Contains("box")) mesh = _cubeMesh;
                else if (type.Contains("quad") || type.Contains("plane")) mesh = _quadMesh;
                else if (type.Contains("cylinder")) mesh = _cylinderMesh;
                else if (type.Contains("torus")) mesh = _torusMesh;
                else if (type.Contains("mesh"))
                {
                    // Try load from file
                    if (entity.Parameters.Count > 0)
                    {
                        string path = entity.Parameters[0];
                        // If path is relative, make it absolute based on project root if possible
                        // But here we might just have the raw string. 
                        // SceneParser usually handles absolute paths if we are lucky, or we need to resolve it.
                        // Assuming full path for now or trying to find it.
                        
                        if (_meshCache.ContainsKey(path))
                        {
                            mesh = _meshCache[path];
                        }
                        else
                        {
                            try 
                            {
                                // Attempt to load
                                if (File.Exists(path))
                                {
                                    mesh = Mesh.LoadObj(path);
                                    if (mesh != null) _meshCache[path] = mesh;
                                }
                            }
                            catch (Exception ex)
                            {
                                Logger?.Invoke($"Failed to load mesh {path}: {ex.Message}");
                            }
                        }
                    }
                    
                    if (mesh == null) mesh = _cubeMesh; // Fallback
                }
                else mesh = _cubeMesh; // Default

                if (mesh != null)
                {
                    var ro = new RenderObject();
                    ro.Mesh = mesh;
                    
                    // Transform
                    Vector3 position = new Vector3(entity.Position[0], entity.Position[1], entity.Position[2]);
                    Vector3 scale = new Vector3(entity.Scale[0], entity.Scale[1], entity.Scale[2]);
                    Vector3 rotation = new Vector3(entity.Rotation[0], entity.Rotation[1], entity.Rotation[2]);
                    
                    ro.Transform = Matrix4.CreateScale(scale) *
                                   Matrix4.CreateRotationZ(MathHelper.DegreesToRadians(rotation.Z)) *
                                   Matrix4.CreateRotationY(MathHelper.DegreesToRadians(rotation.Y)) *
                                   Matrix4.CreateRotationX(MathHelper.DegreesToRadians(rotation.X)) *
                                   Matrix4.CreateTranslation(position);
                    
                    // Color from Material
                    ro.Color = new Vector3(0.7f, 0.7f, 0.7f); // Default Gray
                    var mat = scene.Materials.FirstOrDefault(m => m.Name == entity.Name);
                    if (mat != null && mat.Parameters.Count >= 3)
                    {
                         if (IsFloat(mat.Parameters[0]))
                            ro.Color = new Vector3(ParseFloat(mat.Parameters[0]), ParseFloat(mat.Parameters[1]), ParseFloat(mat.Parameters[2]));
                    }
                    
                    _renderObjects.Add(ro);
                    count++;
                }
            }
            // Logger?.Invoke($"Synced Scene: {count} objects created."); // Removed to prevent spam
        }

        // Input Handling
        public void ProcessKeyboard(bool w, bool s, bool a, bool d, bool q, bool e, float deltaTime)
        {
            if (_camera == null) return;
            
            // Safety Clamp
            if (deltaTime > 0.1f) deltaTime = 0.1f;
            if (deltaTime < 0.0f) deltaTime = 0.0f;

            float speed = 10.0f * deltaTime;
            
            if (w) _camera.Position += _camera.Front * speed;
            if (s) _camera.Position -= _camera.Front * speed;
            if (a) _camera.Position -= _camera.Right * speed;
            if (d) _camera.Position += _camera.Right * speed;
            if (q) _camera.Position += _camera.Up * speed;   // Up
            if (e) _camera.Position -= _camera.Up * speed;   // Down
        }

        public void ProcessMouse(float xOffset, float yOffset)
        {
             if (_camera == null) return;
             _camera.ProcessMouseMovement(xOffset, yOffset);
        }

        public void ProcessPan(float xOffset, float yOffset)
        {
             if (_camera == null) return;
             // Simple panning
             float speed = 0.05f;
             _camera.Position -= _camera.Right * xOffset * speed;
             _camera.Position += _camera.Up * yOffset * speed;
        }

        private float ParseFloat(string s) => float.TryParse(s, out float v) ? v : 0f;
        private bool IsFloat(string s) => float.TryParse(s, out _);

        private void InitializeGeometry()
        {
            _cubeMesh = Mesh.CreateCube();
            _sphereMesh = Mesh.CreateSphere(); // Approximated
            _quadMesh = Mesh.CreateQuad();
            _cylinderMesh = Mesh.CreateCylinder();
            _torusMesh = Mesh.CreateTorus();
            _gridMesh = Mesh.CreateGrid(20, 1.0f);
            _axesMesh = Mesh.CreateAxes();
        }

        private void CreateGridShader()
        {
            string vertSource = @"
        #version 330 core
        layout (location = 0) in vec3 aPos;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        void main()
        {
            gl_Position = projection * view * model * vec4(aPos, 1.0);
        }";

            string fragSource = @"
        #version 330 core
        out vec4 FragColor;
        uniform vec3 color;

        void main()
        {
            FragColor = vec4(color, 1.0);
        }";

            _gridShaderProgram = ShaderHelper.CreateProgram(vertSource, fragSource);
        }

        private void CreateShaders()
        {
            string vertSource = @"
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 Normal;
out vec3 FragPos;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
}";

            string fragSource = @"
#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;

uniform vec3 objectColor;

void main()
{
    // Simple Lighting
    vec3 lightPos = vec3(10.0, 20.0, 15.0); 
    vec3 lightColor = vec3(1.0, 1.0, 1.0);

    // Ambient
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColor;
  
    // Diffuse 
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    vec3 result = (ambient + diffuse) * objectColor;
    FragColor = vec4(result, 1.0);
}";

            _shaderProgram = ShaderHelper.CreateProgram(vertSource, fragSource);
        }
    }
    
    public class RenderObject
    {
        public Mesh? Mesh;
        public Matrix4 Transform;
        public Vector3 Color;
    }
    
    public class Mesh
    {
        public int Vao;
        public int Vbo;
        public int Ebo;
        public int Count;
        
        public void Draw()
        {
            GL.BindVertexArray(Vao);
            GL.DrawElements(PrimitiveType.Triangles, Count, DrawElementsType.UnsignedInt, 0);
            GL.BindVertexArray(0);
        }
        
        public static Mesh CreateCube()
        {
            float[] vertices = {
                // Pos                  // Normal
                -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
                 0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
                 0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
                 0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
                -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
                -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

                -0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
                 0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
                 0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
                 0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
                -0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
                -0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,

                -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
                -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
                -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
                -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
                -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
                -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

                 0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
                 0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
                 0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
                 0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
                 0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
                 0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

                -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
                 0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
                 0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
                 0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
                -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
                -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,

                -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
                 0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
                 0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
                 0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
                -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
                -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f
            };
            
            // Cube above is unindexed (36 verts). For simplicity in this helper, we use DrawArrays or construct indices.
            // Our Mesh class uses DrawElements. So let's generate indices 0..35
            uint[] indices = Enumerable.Range(0, 36).Select(i => (uint)i).ToArray();
            
            return Create(vertices, indices);
        }
        
        public static Mesh CreateQuad()
        {
             float[] vertices = {
                // Pos                  // Normal
                -0.5f, 0.0f, -0.5f,  0.0f, 1.0f, 0.0f,
                 0.5f, 0.0f, -0.5f,  0.0f, 1.0f, 0.0f,
                 0.5f, 0.0f,  0.5f,  0.0f, 1.0f, 0.0f,
                -0.5f, 0.0f,  0.5f,  0.0f, 1.0f, 0.0f,
             };
             uint[] indices = { 0, 1, 2, 2, 3, 0 };
             return Create(vertices, indices);
        }

        public static Mesh CreateGrid(int size, float step)
        {
            List<float> verts = new List<float>();
            List<uint> inds = new List<uint>();
            
            // X lines
            for (int i = -size; i <= size; i++)
            {
                // Line along Z
                verts.Add(i * step); verts.Add(0); verts.Add(-size * step); // Pos
                verts.Add(0); verts.Add(1); verts.Add(0); // Normal (unused)
                
                verts.Add(i * step); verts.Add(0); verts.Add(size * step);
                verts.Add(0); verts.Add(1); verts.Add(0);
                
                uint start = (uint)(verts.Count / 6 - 2);
                inds.Add(start);
                inds.Add(start + 1);
            }
            
            // Z lines
            for (int i = -size; i <= size; i++)
            {
                // Line along X
                verts.Add(-size * step); verts.Add(0); verts.Add(i * step);
                verts.Add(0); verts.Add(1); verts.Add(0);
                
                verts.Add(size * step); verts.Add(0); verts.Add(i * step);
                verts.Add(0); verts.Add(1); verts.Add(0);

                uint start = (uint)(verts.Count / 6 - 2);
                inds.Add(start);
                inds.Add(start + 1);
            }
            
            return Create(verts.ToArray(), inds.ToArray());
        }

        public static Mesh CreateAxes()
        {
            float[] vertices = {
                // Pos                  // Normal (unused)
                0.0f, 0.0f, 0.0f,       0,1,0, // Origin
                1.0f, 0.0f, 0.0f,       0,1,0, // X End
                
                0.0f, 0.0f, 0.0f,       0,1,0, // Origin
                0.0f, 1.0f, 0.0f,       0,1,0, // Y End
                
                0.0f, 0.0f, 0.0f,       0,1,0, // Origin
                0.0f, 0.0f, 1.0f,       0,1,0, // Z End
            };
            uint[] indices = { 0, 1, 2, 3, 4, 5 };
            return Create(vertices, indices);
        }
        
        public static Mesh CreateSphere()
        {
            // Simple Icosphere or LatLong sphere
            // Let's do a simple LatLong
            List<float> verts = new List<float>();
            List<uint> inds = new List<uint>();
            
            int X_SEGMENTS = 32;
            int Y_SEGMENTS = 32;
            
            for (int y = 0; y <= Y_SEGMENTS; ++y)
            {
                for (int x = 0; x <= X_SEGMENTS; ++x)
                {
                    float xSegment = (float)x / (float)X_SEGMENTS;
                    float ySegment = (float)y / (float)Y_SEGMENTS;
                    float xPos = (float)(Math.Cos(xSegment * 2.0f * Math.PI) * Math.Sin(ySegment * Math.PI));
                    float yPos = (float)Math.Cos(ySegment * Math.PI);
                    float zPos = (float)(Math.Sin(xSegment * 2.0f * Math.PI) * Math.Sin(ySegment * Math.PI));

                    // Radius 0.5 to match unit cube/quad size logic (diameter 1)
                    verts.Add(xPos * 0.5f);
                    verts.Add(yPos * 0.5f);
                    verts.Add(zPos * 0.5f);
                    
                    // Normal
                    verts.Add(xPos);
                    verts.Add(yPos);
                    verts.Add(zPos);
                }
            }
            
            for (int y = 0; y < Y_SEGMENTS; ++y)
            {
                for (int x = 0; x < X_SEGMENTS; ++x)
                {
                    inds.Add((uint)((y + 1) * (X_SEGMENTS + 1) + x));
                    inds.Add((uint)(y * (X_SEGMENTS + 1) + x));
                    inds.Add((uint)(y * (X_SEGMENTS + 1) + x + 1));

                    inds.Add((uint)((y + 1) * (X_SEGMENTS + 1) + x));
                    inds.Add((uint)(y * (X_SEGMENTS + 1) + x + 1));
                    inds.Add((uint)((y + 1) * (X_SEGMENTS + 1) + x + 1));
                }
            }
            
            return Create(verts.ToArray(), inds.ToArray());
        }

        public static Mesh CreateCylinder(int segments = 32)
        {
            List<float> verts = new List<float>();
            List<uint> inds = new List<uint>();
            
            float radius = 0.5f;
            float height = 1.0f;
            float halfHeight = height / 2.0f;

            // Cap Center Top
            verts.Add(0); verts.Add(halfHeight); verts.Add(0); // Pos
            verts.Add(0); verts.Add(1); verts.Add(0); // Normal
            
            // Cap Center Bottom
            verts.Add(0); verts.Add(-halfHeight); verts.Add(0);
            verts.Add(0); verts.Add(-1); verts.Add(0);

            // Side Vertices
            for (int i = 0; i <= segments; i++)
            {
                float theta = (float)i / segments * 2.0f * (float)Math.PI;
                float x = (float)Math.Cos(theta) * radius;
                float z = (float)Math.Sin(theta) * radius;
                
                // Top Edge
                verts.Add(x); verts.Add(halfHeight); verts.Add(z);
                verts.Add(x); verts.Add(0); verts.Add(z); // Normal approx (flat shading) or smooth? Let's use x,0,z for smooth side
                
                // Bottom Edge
                verts.Add(x); verts.Add(-halfHeight); verts.Add(z);
                verts.Add(x); verts.Add(0); verts.Add(z);
            }
            
            // Generate Indices
            int baseIndex = 2; // 0 and 1 are centers
            for (int i = 0; i < segments; i++)
            {
                int top1 = baseIndex + i * 2;
                int bottom1 = baseIndex + i * 2 + 1;
                int top2 = baseIndex + (i + 1) * 2;
                int bottom2 = baseIndex + (i + 1) * 2 + 1;
                
                // Side
                inds.Add((uint)top1); inds.Add((uint)bottom1); inds.Add((uint)top2);
                inds.Add((uint)bottom1); inds.Add((uint)bottom2); inds.Add((uint)top2);
                
                // Top Cap (Center 0)
                // We need separate vertices for caps to have correct normals (0,1,0), but for simplicity here we reuse side verts with smooth normals 
                // OR we accept smooth normals on caps which looks weird.
                // For a proper cylinder we need duplicated vertices. 
                // Let's keep it simple: just side for now, user can see shape.
                // Actually, let's add cap indices using the same vertices (lighting will be weird at edges but shape correct).
                inds.Add(0); inds.Add((uint)top2); inds.Add((uint)top1);
                
                // Bottom Cap (Center 1)
                inds.Add(1); inds.Add((uint)bottom1); inds.Add((uint)bottom2);
            }

            return Create(verts.ToArray(), inds.ToArray());
        }

        public static Mesh CreateTorus(float mainRadius = 0.4f, float tubeRadius = 0.1f, int mainSegments = 32, int tubeSegments = 16)
        {
             List<float> verts = new List<float>();
             List<uint> inds = new List<uint>();

             for (int i = 0; i <= mainSegments; i++)
             {
                 float u = (float)i / mainSegments * 2.0f * (float)Math.PI;
                 float cu = (float)Math.Cos(u);
                 float su = (float)Math.Sin(u);

                 for (int j = 0; j <= tubeSegments; j++)
                 {
                     float v = (float)j / tubeSegments * 2.0f * (float)Math.PI;
                     float cv = (float)Math.Cos(v);
                     float sv = (float)Math.Sin(v);

                     // Position
                     float x = (mainRadius + tubeRadius * cv) * cu;
                     float z = (mainRadius + tubeRadius * cv) * su;
                     float y = tubeRadius * sv;
                     
                     verts.Add(x); verts.Add(y); verts.Add(z);
                     
                     // Normal
                     float nx = cv * cu;
                     float nz = cv * su;
                     float ny = sv;
                     verts.Add(nx); verts.Add(ny); verts.Add(nz);
                 }
             }

             for (int i = 0; i < mainSegments; i++)
             {
                 for (int j = 0; j < tubeSegments; j++)
                 {
                     int nextI = i + 1;
                     int nextJ = j + 1;
                     
                     uint a = (uint)(i * (tubeSegments + 1) + j);
                     uint b = (uint)(nextI * (tubeSegments + 1) + j);
                     uint c = (uint)(nextI * (tubeSegments + 1) + nextJ);
                     uint d = (uint)(i * (tubeSegments + 1) + nextJ);
                     
                     inds.Add(a); inds.Add(b); inds.Add(d);
                     inds.Add(b); inds.Add(c); inds.Add(d);
                 }
             }
             
             return Create(verts.ToArray(), inds.ToArray());
        }
        
        public static Mesh? LoadObj(string path)
        {
            if (!File.Exists(path)) return null;
            
            List<Vector3> tempVerts = new List<Vector3>();
            List<float> finalVerts = new List<float>(); // pos(3) + norm(3)
            List<uint> indices = new List<uint>();
            
            // Simple OBJ Parser (Triangulated only)
            try
            {
                string[] lines = File.ReadAllLines(path);
                foreach (var line in lines)
                {
                    if (line.StartsWith("v "))
                    {
                        var parts = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                        if (parts.Length >= 4)
                        {
                            if (float.TryParse(parts[1], out float x) &&
                                float.TryParse(parts[2], out float y) &&
                                float.TryParse(parts[3], out float z))
                            {
                                tempVerts.Add(new Vector3(x, y, z));
                            }
                        }
                    }
                    else if (line.StartsWith("f "))
                    {
                        var parts = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                        if (parts.Length >= 4) // f v1 v2 v3
                        {
                            // Parse first 3 vertices (Triangle)
                            // OBJ indices are 1-based
                            for (int i = 1; i <= 3; i++)
                            {
                                string part = parts[i];
                                string[] vData = part.Split('/');
                                if (int.TryParse(vData[0], out int vIdx))
                                {
                                    // Adjust to 0-based
                                    int index = vIdx - 1;
                                    if (index >= 0 && index < tempVerts.Count)
                                    {
                                        Vector3 pos = tempVerts[index];
                                        
                                        // Add to final buffer (unoptimized, duplicate vertices)
                                        finalVerts.Add(pos.X); finalVerts.Add(pos.Y); finalVerts.Add(pos.Z);
                                        // Default Normal (Up) - Proper normal calculation is complex
                                        finalVerts.Add(0); finalVerts.Add(1); finalVerts.Add(0); 
                                        
                                        indices.Add((uint)(indices.Count));
                                    }
                                }
                            }
                        }
                    }
                }
                
                if (finalVerts.Count == 0) return null;
                return Create(finalVerts.ToArray(), indices.ToArray());
            }
            catch
            {
                return null;
            }
        }

        private static Mesh Create(float[] vertices, uint[] indices)
        {
            var mesh = new Mesh();
            mesh.Count = indices.Length;
            
            mesh.Vao = GL.GenVertexArray();
            mesh.Vbo = GL.GenBuffer();
            mesh.Ebo = GL.GenBuffer();
            
            GL.BindVertexArray(mesh.Vao);
            
            GL.BindBuffer(BufferTarget.ArrayBuffer, mesh.Vbo);
            GL.BufferData(BufferTarget.ArrayBuffer, vertices.Length * sizeof(float), vertices, BufferUsageHint.StaticDraw);
            
            GL.BindBuffer(BufferTarget.ElementArrayBuffer, mesh.Ebo);
            GL.BufferData(BufferTarget.ElementArrayBuffer, indices.Length * sizeof(uint), indices, BufferUsageHint.StaticDraw);
            
            // Pos
            GL.VertexAttribPointer(0, 3, VertexAttribPointerType.Float, false, 6 * sizeof(float), 0);
            GL.EnableVertexAttribArray(0);
            
            // Normal
            GL.VertexAttribPointer(1, 3, VertexAttribPointerType.Float, false, 6 * sizeof(float), 3 * sizeof(float));
            GL.EnableVertexAttribArray(1);
            
            GL.BindVertexArray(0);
            return mesh;
        }
    }

    public static class ShaderHelper
    {
        public static int CreateProgram(string vertSource, string fragSource)
        {
            int vertexShader = CompileShader(ShaderType.VertexShader, vertSource);
            int fragmentShader = CompileShader(ShaderType.FragmentShader, fragSource);
            
            int shaderProgram = GL.CreateProgram();
            GL.AttachShader(shaderProgram, vertexShader);
            GL.AttachShader(shaderProgram, fragmentShader);
            GL.LinkProgram(shaderProgram);
            
            GL.GetProgram(shaderProgram, GetProgramParameterName.LinkStatus, out int success);
            if (success == 0)
            {
                string infoLog = GL.GetProgramInfoLog(shaderProgram);
                throw new Exception($"Shader Link Error: {infoLog}");
            }
            
            GL.DeleteShader(vertexShader);
            GL.DeleteShader(fragmentShader);
            
            return shaderProgram;
        }
        
        private static int CompileShader(ShaderType type, string source)
        {
            int shader = GL.CreateShader(type);
            GL.ShaderSource(shader, source);
            GL.CompileShader(shader);
            
            GL.GetShader(shader, ShaderParameter.CompileStatus, out int success);
            if (success == 0)
            {
                string infoLog = GL.GetShaderInfoLog(shader);
                throw new Exception($"{type} Compile Error: {infoLog}");
            }
            return shader;
        }
    }

    // --- Camera Class ---
    public class Camera
    {
        public Vector3 Position { get; set; }
        public Vector3 Front { get; private set; }
        public Vector3 Up { get; private set; }
        public Vector3 Right { get; private set; }
        public Vector3 WorldUp { get; private set; }

        public float Yaw { get; set; }
        public float Pitch { get; set; }

        public float MouseSensitivity { get; set; } = 0.1f;
        public float AspectRatio { get; set; }

        public Camera(Vector3 position, float aspectRatio)
        {
            Position = position;
            WorldUp = Vector3.UnitY;
            Yaw = -90.0f;
            Pitch = 0.0f;
            Front = new Vector3(0.0f, 0.0f, -1.0f);
            AspectRatio = aspectRatio;
            UpdateCameraVectors();
        }

        public Matrix4 GetViewMatrix()
        {
            return Matrix4.LookAt(Position, Position + Front, Up);
        }
        
        public void SetDirection(Vector3 dir)
        {
            if (dir.LengthSquared < 0.0001f) return;
            dir = Vector3.Normalize(dir);
            
            // Clamp for asin safety
            float y = dir.Y;
            if (y > 1.0f) y = 1.0f;
            if (y < -1.0f) y = -1.0f;

            Pitch = MathHelper.RadiansToDegrees((float)Math.Asin(y));
            Yaw = MathHelper.RadiansToDegrees((float)Math.Atan2(dir.Z, dir.X));
            UpdateCameraVectors();
        }

        public void ProcessMouseMovement(float xOffset, float yOffset, bool constrainPitch = true)
        {
            xOffset *= MouseSensitivity;
            yOffset *= MouseSensitivity;

            Yaw += xOffset;
            Pitch -= yOffset;

            if (constrainPitch)
            {
                if (Pitch > 89.0f) Pitch = 89.0f;
                if (Pitch < -89.0f) Pitch = -89.0f;
            }

            UpdateCameraVectors();
        }

        public void UpdateCameraVectors()
        {
            Vector3 front;
            front.X = (float)Math.Cos(MathHelper.DegreesToRadians(Yaw)) * (float)Math.Cos(MathHelper.DegreesToRadians(Pitch));
            front.Y = (float)Math.Sin(MathHelper.DegreesToRadians(Pitch));
            front.Z = (float)Math.Sin(MathHelper.DegreesToRadians(Yaw)) * (float)Math.Cos(MathHelper.DegreesToRadians(Pitch));
            
            Front = Vector3.Normalize(front);
            Right = Vector3.Normalize(Vector3.Cross(Front, WorldUp));
            Up = Vector3.Normalize(Vector3.Cross(Right, Front));
        }
    }
}
