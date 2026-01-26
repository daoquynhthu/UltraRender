using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media.Imaging;
using System.Windows.Input;
using System.Windows.Threading;

using System.Windows.Controls;
using Microsoft.Win32;
using OpenTK.Wpf;

namespace UltraRender.GUI
{
    public partial class MainWindow : Window
    {
        public static RoutedCommand RenderCommand = new RoutedCommand();
        private RealtimeRenderer? _realtimeRenderer;
        
        private DispatcherTimer _updateTimer;
        private string _currentOutputPath = "";
        private string _currentScenePath = "";
        private DateTime _lastFileTime;

        private Process? _currentProcess;
        private string _currentProjectRoot = "";
        private string _currentProjectFile = "";

        public string CurrentProjectRoot => _currentProjectRoot; // Expose for Crash Logging

        private System.Collections.Generic.List<string> _recentProjects = new System.Collections.Generic.List<string>();
        private const int MaxRecentProjects = 10;
        private static string _debugLogPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "debug_log.txt");

        public MainWindow()
        {
            // Reset log file on start
            try { File.WriteAllText(_debugLogPath, $"[{DateTime.Now}] Session Started\n"); } catch {}

            InitializeComponent();
            
            // Version Identification
            string version = "v1.2 (Build 2026-01-26 11:45)";
            this.Title = $"UltraRender Studio - {version}";
            AppendLog($"UltraRender GUI Ready - {version}");

            InitializeShortcuts();
            Closing += MainWindow_Closing;
            _updateTimer = new DispatcherTimer();
            _updateTimer.Interval = TimeSpan.FromMilliseconds(500);
            _updateTimer.Tick += UpdateTimer_Tick;
            
            txtConsole.Text = "UltraRender GUI Ready (Safe Mode).\n";
            _updateTimer.Start();
            
            // Set initial UI state
            txtCurrentPath.Text = "No Project Open";
            LoadRecentProjects();
            
            AppendLog($"Log File: {_debugLogPath}");
        }

        private void InitializeShortcuts()
        {
            // Bind Commands
            CommandBindings.Add(new CommandBinding(ApplicationCommands.Open, OpenScene_Executed));
            CommandBindings.Add(new CommandBinding(ApplicationCommands.Save, SaveImage_Executed));
            CommandBindings.Add(new CommandBinding(RenderCommand, Render_Executed));

            // Setup Input Gestures (if not in XAML)
            InputBindings.Add(new KeyBinding(ApplicationCommands.Open, Key.O, ModifierKeys.Control));
            InputBindings.Add(new KeyBinding(ApplicationCommands.Save, Key.S, ModifierKeys.Control));
            InputBindings.Add(new KeyBinding(RenderCommand, Key.F5, ModifierKeys.None));
        }

        private void LoadRecentProjects()
        {
            try
            {
                string recentFile = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "recent_projects.txt");
                if (File.Exists(recentFile))
                {
                    _recentProjects = new System.Collections.Generic.List<string>(File.ReadAllLines(recentFile));
                    UpdateRecentMenu();
                }
            }
            catch { }
        }

        private void AddToRecent(string path)
        {
            try
            {
                // Remove if exists to move to top
                _recentProjects.RemoveAll(p => p.Equals(path, StringComparison.OrdinalIgnoreCase));
                _recentProjects.Insert(0, path);
                
                if (_recentProjects.Count > MaxRecentProjects)
                {
                    _recentProjects.RemoveRange(MaxRecentProjects, _recentProjects.Count - MaxRecentProjects);
                }

                UpdateRecentMenu();
                
                // Save
                string recentFile = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "recent_projects.txt");
                File.WriteAllLines(recentFile, _recentProjects);
            }
            catch { }
        }

        private void UpdateRecentMenu()
        {
            menuOpenRecent.Items.Clear();
            if (_recentProjects.Count == 0)
            {
                menuOpenRecent.IsEnabled = false;
                return;
            }

            menuOpenRecent.IsEnabled = true;
            foreach (var path in _recentProjects)
            {
                var item = new MenuItem { Header = path };
                item.Click += (s, e) => OpenProject(path);
                menuOpenRecent.Items.Add(item);
            }
            
            menuOpenRecent.Items.Add(new Separator());
            var clearItem = new MenuItem { Header = "Clear Recent" };
            clearItem.Click += (s, e) => 
            { 
                _recentProjects.Clear(); 
                UpdateRecentMenu();
                try { File.Delete(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "recent_projects.txt")); } catch {}
            };
            menuOpenRecent.Items.Add(clearItem);
        }

        // --- Project Management ---

        private void MenuItem_NewProject_Click(object sender, RoutedEventArgs e)
        {
            // Use WinForms dialog or common OpenFolderDialog if possible. 
            // Since we are in WPF, we can use OpenFolderDialog from Microsoft.Win32 (Net 8.0+) or fallback.
            var dialog = new OpenFolderDialog();
            dialog.Title = "Select New Project Location";
            dialog.Multiselect = false;
            
            if (dialog.ShowDialog() == true)
            {
                string projectPath = dialog.FolderName;
                // Ideally ask for project name, but for now use folder name or create a subfolder
                CreateProject(projectPath);
            }
        }

        private void MenuItem_OpenProject_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new OpenFileDialog();
            dialog.Filter = "Project Files (*.project)|*.project";
            
            if (dialog.ShowDialog() == true)
            {
                OpenProject(dialog.FileName);
            }
        }

        private void MenuItem_SaveProject_Click(object sender, RoutedEventArgs e)
        {
            if (!string.IsNullOrEmpty(_currentProjectFile))
            {
                SaveProject(_currentProjectFile);
            }
            else
            {
                MenuItem_SaveProjectAs_Click(sender, e);
            }
        }

        private void MenuItem_SaveProjectAs_Click(object sender, RoutedEventArgs e)
        {
            var dialog = new SaveFileDialog();
            dialog.Filter = "Project Files (*.project)|*.project";
            
            if (dialog.ShowDialog() == true)
            {
                SaveProject(dialog.FileName);
            }
        }

        private void MenuItem_SaveScene_Click(object sender, RoutedEventArgs e)
        {
            if (_currentSceneData != null && !string.IsNullOrEmpty(_currentScenePath))
            {
                try
                {
                    _currentSceneData.Save(_currentScenePath);
                    AppendLog($"Scene saved: {_currentScenePath}");
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Failed to save scene: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
            else
            {
                MessageBox.Show("No scene loaded to save.", "Warning", MessageBoxButton.OK, MessageBoxImage.Warning);
            }
        }

        private void CreateProject(string rootPath)
        {
            try
            {
                // Create standard directories
                string[] dirs = { "Scenes", "Textures", "Models", "Audio", "Materials", "Prefabs", "Scripts" };
                foreach (var dir in dirs)
                {
                    Directory.CreateDirectory(Path.Combine(rootPath, dir));
                }

                // Create .project file
                string projectFile = Path.Combine(rootPath, "Project.project");
                ProjectConfig config = new ProjectConfig { Name = new DirectoryInfo(rootPath).Name, Version = "1.0" };
                string json = System.Text.Json.JsonSerializer.Serialize(config);
                File.WriteAllText(projectFile, json);

                AppendLog($"Created new project at: {rootPath}");
                OpenProject(projectFile);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to create project: {ex.Message}");
            }
        }

        private void OpenProject(string projectFile)
        {
            try
            {
                if (!File.Exists(projectFile)) return;

                _currentProjectFile = projectFile;
                _currentProjectRoot = Path.GetDirectoryName(projectFile) ?? "";
                
                txtCurrentPath.Text = _currentProjectRoot;
                AppendLog($"Opened project: {projectFile}");
                AddToRecent(projectFile);

                // Load Project Config (Layout & Last Scene)
                try
                {
                    string json = File.ReadAllText(projectFile);
                    ProjectConfig config = System.Text.Json.JsonSerializer.Deserialize<ProjectConfig>(json) ?? new ProjectConfig();
                    
                    // Restore Window Layout
                    if (config.WindowWidth > 100 && config.WindowHeight > 100)
                    {
                        this.Width = config.WindowWidth;
                        this.Height = config.WindowHeight;
                    }
                    if (config.WindowMaximized)
                    {
                        this.WindowState = WindowState.Maximized;
                    }
                    
                    // Restore Last Scene
                    if (!string.IsNullOrEmpty(config.LastScene) && File.Exists(config.LastScene))
                    {
                         // IMPORTANT: Do NOT force load here if we are just starting up or switching.
                         // Let the user choose? Or load it?
                         // Current logic loads it.
                         // But we must ensure it doesn't conflict with any manual action.
                         // For now, we trust this, but we log it.
                         
                         _currentScenePath = config.LastScene;
                         LoadSceneData(config.LastScene);
                         AppendLog($"[Auto-Recover] Restored last scene: {Path.GetFileName(config.LastScene)}");
                    }
                }
                catch (Exception ex)
                {
                    AppendLog($"Warning: Failed to load project config: {ex.Message}");
                }

                // Refresh Asset Browser (Default to All Assets or first tab)
                if (tabAssets.SelectedItem is TabItem item)
                {
                     RefreshAssets(item.Tag as string);
                }
                else
                {
                    RefreshAssets("");
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to open project: {ex.Message}");
            }
        }

        private void SaveProject(string projectFile)
        {
            // Currently just updates the file timestamp or saves modified config
            try
            {
                ProjectConfig config = new ProjectConfig 
                { 
                    Name = new DirectoryInfo(_currentProjectRoot).Name, 
                    Version = "1.0",
                    LastScene = _currentScenePath,
                    WindowWidth = this.ActualWidth,
                    WindowHeight = this.ActualHeight,
                    WindowMaximized = (this.WindowState == WindowState.Maximized)
                };
                
                string json = System.Text.Json.JsonSerializer.Serialize(config);
                File.WriteAllText(projectFile, json);
                AppendLog("Project saved.");
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to save project: {ex.Message}");
            }
        }

        // --- Asset Browser Logic ---

        private void TabAssets_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (e.Source is TabControl && tabAssets.SelectedItem is TabItem item)
            {
                RefreshAssets(item.Tag as string);
            }
        }

        private void TxtAssetSearch_TextChanged(object sender, TextChangedEventArgs e)
        {
            if (tabAssets.SelectedItem is TabItem item)
            {
                RefreshAssets(item.Tag as string, txtAssetSearch.Text);
            }
        }

        private void RefreshAssets(string? category, string filter = "")
        {
            listAssets.ItemsSource = null;
            if (string.IsNullOrEmpty(_currentProjectRoot)) return;

            var assets = new System.Collections.Generic.List<AssetFile>();

            try
            {
                if (string.IsNullOrEmpty(category)) // All Assets
                {
                    // Recursively scan all standard folders
                    string[] dirs = { "Scenes", "Textures", "Models", "Audio", "Materials", "Prefabs", "Scripts" };
                    foreach (var dir in dirs)
                    {
                        string path = Path.Combine(_currentProjectRoot, dir);
                        if (Directory.Exists(path))
                        {
                            AddFilesFromDir(path, assets, filter);
                        }
                    }
                }
                else // Specific Category
                {
                    // Map category tag to folder name (case-insensitive usually, but here strict)
                    // The tags are lowercase "scenes", folders are TitleCase "Scenes" usually.
                    // Let's handle case insensitivity or mapping.
                    string targetDir = char.ToUpper(category[0]) + category.Substring(1); // "scenes" -> "Scenes"
                    string path = Path.Combine(_currentProjectRoot, targetDir);
                    
                    if (Directory.Exists(path))
                    {
                        AddFilesFromDir(path, assets, filter);
                    }
                }
            }
            catch (Exception ex)
            {
                AppendLog($"Error listing assets: {ex.Message}");
            }

            listAssets.ItemsSource = assets;
        }

        private void AddFilesFromDir(string dirPath, System.Collections.Generic.List<AssetFile> list, string filter)
        {
            foreach (var file in Directory.GetFiles(dirPath))
            {
                string name = Path.GetFileName(file);
                if (!string.IsNullOrEmpty(filter) && !name.Contains(filter, StringComparison.OrdinalIgnoreCase)) continue;

                list.Add(new AssetFile 
                { 
                    Name = name, 
                    FullPath = file,
                    Icon = GetIconForFile(name),
                    Color = GetColorForFile(name)
                });
            }
        }

        private string GetIconForFile(string name)
        {
            string ext = Path.GetExtension(name).ToLower();
            return ext switch
            {
                ".scene" => "🎬",
                ".bmp" or ".png" or ".jpg" => "🖼️",
                ".obj" or ".fbx" => "🧊",
                ".wav" or ".mp3" => "🔊",
                ".mat" => "🎨",
                ".cs" => "📜",
                _ => "📄"
            };
        }

        private System.Windows.Media.Brush GetColorForFile(string name)
        {
             string ext = Path.GetExtension(name).ToLower();
             return ext switch
             {
                 ".scene" => System.Windows.Media.Brushes.MediumPurple,
                 ".bmp" or ".png" or ".jpg" => System.Windows.Media.Brushes.Cyan,
                 ".obj" or ".fbx" => System.Windows.Media.Brushes.Orange,
                 ".wav" or ".mp3" => System.Windows.Media.Brushes.Yellow,
                 ".mat" => System.Windows.Media.Brushes.Magenta,
                 ".cs" => System.Windows.Media.Brushes.LightGreen,
                 _ => System.Windows.Media.Brushes.Gray
             };
        }

        private void ListAssets_Drop(object sender, DragEventArgs e)
        {
            if (string.IsNullOrEmpty(_currentProjectRoot)) return;
            
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                string[] files = (string[])e.Data.GetData(DataFormats.FileDrop);
                // Determine target folder based on current tab
                string targetFolder = "Assets"; // Default fallback
                if (tabAssets.SelectedItem is TabItem item && item.Tag is string tag && !string.IsNullOrEmpty(tag))
                {
                     targetFolder = char.ToUpper(tag[0]) + tag.Substring(1);
                }
                else
                {
                    // If "All Assets", try to infer from extension? Or just put in root?
                    // Let's default to "Textures" for images, "Scenes" for .scene, etc.
                    // For now, just put in root of project if no specific tab, or maybe "Others".
                    // Simpler: Ask user or just copy to current view if possible. 
                    // Let's implement simple inference.
                }

                foreach (var file in files)
                {
                    // Simple inference if no specific tab selected
                    string destFolder = targetFolder;
                    if (targetFolder == "Assets")
                    {
                        string ext = Path.GetExtension(file).ToLower();
                        if (ext == ".scene") destFolder = "Scenes";
                        else if (ext == ".png" || ext == ".bmp") destFolder = "Textures";
                        else if (ext == ".obj") destFolder = "Models";
                        else destFolder = "Textures"; // Default
                    }

                    string destPath = Path.Combine(_currentProjectRoot, destFolder, Path.GetFileName(file));
                    try 
                    {
                        Directory.CreateDirectory(Path.GetDirectoryName(destPath)!);
                        File.Copy(file, destPath, true);
                        AppendLog($"Imported: {Path.GetFileName(file)}");
                    }
                    catch (Exception ex)
                    {
                        AppendLog($"Import failed: {ex.Message}");
                    }
                }
                
                // Refresh
                if (tabAssets.SelectedItem is TabItem currentItem)
                    RefreshAssets(currentItem.Tag as string);
            }
        }


        private void OpenScene_Executed(object sender, ExecutedRoutedEventArgs e)
        {
            BtnBrowseScene_Click(sender, e);
        }

        private void SaveImage_Executed(object sender, ExecutedRoutedEventArgs e)
        {
            MenuItem_SaveImage_Click(sender, e);
        }

        private void Render_Executed(object sender, ExecutedRoutedEventArgs e)
        {
            if (btnRender.IsEnabled)
            {
                BtnRender_Click(sender, e);
            }
        }

        private string FindProjectRoot()
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            DirectoryInfo rootDir = new DirectoryInfo(baseDir);
            for (int i = 0; i < 5; i++)
            {
                if (rootDir.Parent != null) rootDir = rootDir.Parent;
            }
            return rootDir.FullName;
        }

        private void BtnBrowseScene_Click(object sender, RoutedEventArgs e)
        {
            OpenFileDialog openFileDialog = new OpenFileDialog();
            openFileDialog.Filter = "Scene Files (*.scene)|*.scene|All files (*.*)|*.*";
            string root = FindProjectRoot();
            if (!string.IsNullOrEmpty(root))
            {
                 string sceneDir = Path.Combine(root, "scenes");
                 if (Directory.Exists(sceneDir)) openFileDialog.InitialDirectory = sceneDir;
            }

            if (openFileDialog.ShowDialog() == true)
            {
                _currentScenePath = openFileDialog.FileName;
                LoadSceneData(openFileDialog.FileName);
            }
        }

        private void MenuItem_OpenScene_Click(object sender, RoutedEventArgs e)
        {
            BtnBrowseScene_Click(sender, e);
        }

        private void MenuItem_SaveImage_Click(object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrEmpty(_currentOutputPath) || !File.Exists(_currentOutputPath))
            {
                MessageBox.Show("No rendered image to save.", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }

            SaveFileDialog saveFileDialog = new SaveFileDialog();
            saveFileDialog.Filter = "Bitmap Image (*.bmp)|*.bmp|PNG Image (*.png)|*.png";
            saveFileDialog.FileName = "render_output.bmp";
            
            if (saveFileDialog.ShowDialog() == true)
            {
                try
                {
                    // If source is BMP and target is BMP, just copy
                    if (Path.GetExtension(saveFileDialog.FileName).ToLower() == ".bmp")
                    {
                         File.Copy(_currentOutputPath, saveFileDialog.FileName, true);
                    }
                    else
                    {
                        // Convert if needed (e.g. to PNG)
                        // Load bitmap (cached in memory)
                        if (imgPreview.Source is BitmapSource source)
                        {
                            BitmapEncoder encoder;
                            if (Path.GetExtension(saveFileDialog.FileName).ToLower() == ".png")
                                encoder = new PngBitmapEncoder();
                            else
                                encoder = new BmpBitmapEncoder();
                                
                            encoder.Frames.Add(BitmapFrame.Create(source));
                            using (var fs = new FileStream(saveFileDialog.FileName, FileMode.Create))
                            {
                                encoder.Save(fs);
                            }
                        }
                    }
                    AppendLog($"Image saved to: {saveFileDialog.FileName}");
                }
                catch (Exception ex)
                {
                    MessageBox.Show($"Failed to save image: {ex.Message}", "Error", MessageBoxButton.OK, MessageBoxImage.Error);
                }
            }
        }

        private SceneData? _currentSceneData;

        private bool _isSceneLoading = false;

        private void LoadSceneData(string path)
        {
            if (_isSceneLoading)
            {
                AppendLog("[WARN] Scene load collision. Resetting state...");
                _isSceneLoading = false;
            }

            _isSceneLoading = true;
            AppendLog($"Loading Scene: {path}...");

            try
            {
                _realtimeRenderer?.ResetScene();
                
                _currentSceneData = SceneParser.Parse(path);
                
                // Update UI Resolution (Informational)
                txtWidth.Text = _currentSceneData.Width.ToString();
                txtHeight.Text = _currentSceneData.Height.ToString();

                // Populate TreeView
                treeSceneGraph.Items.Clear();

                // Root Node
                var root = new TreeViewItem { Header = "Scene Root", IsExpanded = true, Foreground = System.Windows.Media.Brushes.White };
                
                // Settings Group
                var settingsNode = new TreeViewItem { Header = "Settings", IsExpanded = true };
                if (_currentSceneData.Resolution != null) settingsNode.Items.Add(CreateTreeItem(_currentSceneData.Resolution));
                if (_currentSceneData.Camera != null) settingsNode.Items.Add(CreateTreeItem(_currentSceneData.Camera));
                root.Items.Add(settingsNode);

                // Materials Group
                var materialsNode = new TreeViewItem { Header = $"Materials ({_currentSceneData.Materials.Count()})", IsExpanded = true };
                foreach (var mat in _currentSceneData.Materials)
                {
                    materialsNode.Items.Add(CreateTreeItem(mat));
                }
                root.Items.Add(materialsNode);

                // Entities Group
                var entitiesNode = new TreeViewItem { Header = $"Entities ({_currentSceneData.Entities.Count()})", IsExpanded = true };
                foreach (var ent in _currentSceneData.Entities)
                {
                    entitiesNode.Items.Add(CreateTreeItem(ent));
                }
                root.Items.Add(entitiesNode);

                treeSceneGraph.Items.Add(root);
                AppendLog($"Scene Loaded: {Path.GetFileName(path)} ({_currentSceneData.Entities.Count()} objects)");
            }
            catch (Exception ex)
            {
                AppendLog($"Error parsing scene: {ex.Message}");
                _currentSceneData = null; // Prevent stale data
                _realtimeRenderer?.ResetScene();
            }
            finally
            {
                _isSceneLoading = false;
            }
        }

        private TreeViewItem CreateTreeItem(SceneObject obj)
        {
            // We use the object itself as Header so the DataTemplate in XAML can bind to DisplayName
            return new TreeViewItem { Header = obj, Tag = obj };
        }

        private void TreeSceneGraph_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            panelProperties.Children.Clear();
            
            if (e.NewValue is TreeViewItem item && item.Tag is SceneObject obj)
            {
                GeneratePropertiesUI(obj);
            }
        }

        private void GeneratePropertiesUI(SceneObject obj)
        {
            panelProperties.Children.Clear();

            if (obj == null)
            {
                var txt = new TextBlock { 
                    Text = "No object selected", 
                    Foreground = System.Windows.Media.Brushes.Gray, 
                    HorizontalAlignment = HorizontalAlignment.Center, 
                    Margin = new Thickness(0, 20, 0, 0) 
                };
                panelProperties.Children.Add(txt);
                return;
            }

            // 1. Header
            var header = new TextBlock { 
                Text = (string.IsNullOrEmpty(obj.Name) ? obj.Type : obj.Name).ToUpper(), 
                FontWeight = FontWeights.Bold, 
                Foreground = System.Windows.Media.Brushes.White,
                FontSize = 14,
                Margin = new Thickness(0, 0, 0, 10) 
            };
            panelProperties.Children.Add(header);

            // 2. Transform Section (if applicable)
            if (obj.HasTransform || obj.Command == "add_entity" || obj.Command == "camera")
            {
                AddGroupHeader("TRANSFORM");
                
                // Position
                AddVector3Field("Position", obj.Position, (x, y, z) => {
                    obj.Position[0] = x; obj.Position[1] = y; obj.Position[2] = z;
                });

                // Rotation (Only if applicable, but we show it for entities)
                if (obj.HasRotation || obj.Command == "add_entity")
                {
                    AddVector3Field("Rotation", obj.Rotation, (x, y, z) => {
                        obj.Rotation[0] = x; obj.Rotation[1] = y; obj.Rotation[2] = z;
                        obj.HasRotation = true; // Force flag if edited
                    });
                }

                // Scale (Entities only usually)
                if (obj.Command == "add_entity")
                {
                    AddVector3Field("Scale", obj.Scale, (x, y, z) => {
                        obj.Scale[0] = x; obj.Scale[1] = y; obj.Scale[2] = z;
                    });
                }
            }

            // 3. Properties Section
            AddGroupHeader("PROPERTIES");

            // Special handling based on Command and Type
            if (obj.Command == "define_material")
            {
                GenerateMaterialProperties(obj);
            }
            else if (obj.Command == "add_entity")
            {
                GenerateEntityProperties(obj);
            }
            else if (obj.Command == "camera")
            {
                // Basic camera params support
                if (obj.Parameters.Count >= 3)
                {
                    AddVector3Field("Look At", new float[] { 
                        float.Parse(obj.Parameters[0]), 
                        float.Parse(obj.Parameters[1]), 
                        float.Parse(obj.Parameters[2]) 
                    }, (x,y,z) => {
                        obj.Parameters[0] = x.ToString();
                        obj.Parameters[1] = y.ToString();
                        obj.Parameters[2] = z.ToString();
                    });
                }
                
                if (obj.Parameters.Count >= 4)
                {
                    AddNamedParam(obj, 3, "FOV");
                }
            }
            else
            {
                // Generic Params
                for (int i = 0; i < obj.Parameters.Count; i++)
                {
                    int idx = i;
                    AddPropertyField($"Param {i+1}", obj.Parameters[i], val => obj.Parameters[idx] = val);
                }
            }
        }

        private void GenerateMaterialProperties(SceneObject obj)
        {
            // Common: First 3 are usually Color (Albedo/Emission)
            int currentIdx = 0;
            if (obj.Parameters.Count >= 3 && IsFloat(obj.Parameters[0]) && IsFloat(obj.Parameters[1]) && IsFloat(obj.Parameters[2]))
            {
                string label = obj.Type == "light" ? "Emission" : "Albedo";
                AddColorField(label, obj.Parameters, 0);
                currentIdx = 3;
            }

            // Type specific
            if (obj.Type == "lambertian")
            {
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Roughness");
            }
            else if (obj.Type == "metal")
            {
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Roughness");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "IOR");
                if (currentIdx + 2 < obj.Parameters.Count) 
                {
                    AddColorField("Extinction", obj.Parameters, currentIdx);
                    currentIdx += 3;
                }
            }
            else if (obj.Type == "dielectric")
            {
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Roughness");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "IOR");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Dispersion");
            }

            // Handle remaining/optional parameters (thin_film, etc.)
            // These often come in pairs like "thin_film" "0.5" "1.5"
            while (currentIdx < obj.Parameters.Count)
            {
                string p = obj.Parameters[currentIdx];
                if (p == "thin_film")
                {
                    currentIdx++; // skip keyword
                    if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Film Thk");
                    if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Film IOR");
                }
                else if (p == "density")
                {
                    currentIdx++;
                    if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Density");
                }
                else if (p == "scatter")
                {
                    currentIdx++;
                    if (currentIdx + 2 < obj.Parameters.Count) 
                    {
                        AddColorField("Scatter", obj.Parameters, currentIdx);
                        currentIdx += 3;
                    }
                }
                else if (p == "absorb")
                {
                    currentIdx++;
                    if (currentIdx + 2 < obj.Parameters.Count) 
                    {
                        AddColorField("Absorb", obj.Parameters, currentIdx);
                        currentIdx += 3;
                    }
                }
                else
                {
                    // Unknown or unhandled
                    AddNamedParam(obj, currentIdx++, $"Param {currentIdx}");
                }
            }
        }

        private void GenerateEntityProperties(SceneObject obj)
        {
            int currentIdx = 0;
            
            if (obj.Type == "mesh_cylinder")
            {
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Radius");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Height");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Segments");
            }
            else if (obj.Type == "mesh_cup")
            {
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Radius");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Height");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Thickness");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Segments");
            }
            else if (obj.Type == "mesh_torus")
            {
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Major R");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Minor R");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Major Seg");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Minor Seg");
            }
            else if (obj.Type == "highpoly_sphere")
            {
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Slices");
                if (currentIdx < obj.Parameters.Count) AddNamedParam(obj, currentIdx++, "Stacks");
            }
            
            // Render remaining as generic
            for (int i = currentIdx; i < obj.Parameters.Count; i++)
            {
                AddNamedParam(obj, i, $"Param {i+1}");
            }
        }

        private void AddNamedParam(SceneObject obj, int index, string name)
        {
             AddPropertyField(name, obj.Parameters[index], val => obj.Parameters[index] = val);
        }

        private void AddGroupHeader(string title)
        {
            var border = new Border { 
                Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(45, 45, 48)), 
                Padding = new Thickness(5),
                Margin = new Thickness(0, 10, 0, 5),
                CornerRadius = new CornerRadius(3)
            };
            border.Child = new TextBlock { 
                Text = title, 
                FontWeight = FontWeights.Bold, 
                Foreground = System.Windows.Media.Brushes.LightGray,
                FontSize = 11
            };
            panelProperties.Children.Add(border);
        }

        private void AddVector3Field(string label, float[] values, Action<float, float, float> onUpdate)
        {
            var grid = new Grid { Margin = new Thickness(0, 2, 0, 2) };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(60) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            
            var lbl = new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Top, Foreground = System.Windows.Media.Brushes.Gray, FontSize = 11, Margin = new Thickness(0,4,0,0) };
            Grid.SetColumn(lbl, 0);
            grid.Children.Add(lbl);

            var stack = new StackPanel { Orientation = Orientation.Vertical };
            Grid.SetColumn(stack, 1);
            grid.Children.Add(stack);

            // X
            stack.Children.Add(CreateAxisInput("X", values[0], System.Windows.Media.Brushes.IndianRed, v => { values[0] = v; onUpdate(values[0], values[1], values[2]); }));
            // Y
            stack.Children.Add(CreateAxisInput("Y", values[1], System.Windows.Media.Brushes.SeaGreen, v => { values[1] = v; onUpdate(values[0], values[1], values[2]); }));
            // Z
            stack.Children.Add(CreateAxisInput("Z", values[2], System.Windows.Media.Brushes.CornflowerBlue, v => { values[2] = v; onUpdate(values[0], values[1], values[2]); }));

            panelProperties.Children.Add(grid);
        }

        private UIElement CreateAxisInput(string axis, float value, System.Windows.Media.Brush color, Action<float> onChanged)
        {
            var g = new Grid { Margin = new Thickness(0, 1, 0, 1) };
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(15) });
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var l = new TextBlock { Text = axis, Foreground = color, FontWeight = FontWeights.Bold, VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Center };
            var t = new TextBox { Text = value.ToString("0.###"), Style = (Style)FindResource("DarkTextBox") };
            
            t.TextChanged += (s, e) => {
                if (float.TryParse(t.Text, out float res)) onChanged(res);
            };

            Grid.SetColumn(l, 0);
            Grid.SetColumn(t, 1);
            g.Children.Add(l);
            g.Children.Add(t);
            return g;
        }

        private void AddColorField(string label, List<string> parameters, int startIndex)
        {
            var grid = new Grid { Margin = new Thickness(0, 5, 0, 5) };
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            // Preview Box
            var preview = new Border { Height = 20, CornerRadius = new CornerRadius(2), Margin = new Thickness(0,0,0,5) };
            
            Action updateColor = () => {
                if (float.TryParse(parameters[startIndex], out float r) &&
                    float.TryParse(parameters[startIndex+1], out float g) &&
                    float.TryParse(parameters[startIndex+2], out float b))
                {
                    // Clamp to 0-1 for display
                    byte R = (byte)(Math.Min(1, Math.Max(0, r)) * 255);
                    byte G = (byte)(Math.Min(1, Math.Max(0, g)) * 255);
                    byte B = (byte)(Math.Min(1, Math.Max(0, b)) * 255);
                    preview.Background = new System.Windows.Media.SolidColorBrush(System.Windows.Media.Color.FromRgb(R, G, B));
                }
            };
            updateColor();
            
            grid.Children.Add(preview);

            // Sliders
            var stack = new StackPanel { Orientation = Orientation.Vertical };
            Grid.SetRow(stack, 1);
            grid.Children.Add(stack);

            stack.Children.Add(CreateColorSlider("R", parameters, startIndex, System.Windows.Media.Brushes.IndianRed, updateColor));
            stack.Children.Add(CreateColorSlider("G", parameters, startIndex+1, System.Windows.Media.Brushes.SeaGreen, updateColor));
            stack.Children.Add(CreateColorSlider("B", parameters, startIndex+2, System.Windows.Media.Brushes.CornflowerBlue, updateColor));

            panelProperties.Children.Add(grid);
        }

        private UIElement CreateColorSlider(string label, List<string> parameters, int index, System.Windows.Media.Brush color, Action onUpdate)
        {
            var g = new Grid { Margin = new Thickness(0, 1, 0, 1) };
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(15) });
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(40) });

            float val = float.Parse(parameters[index]);

            var l = new TextBlock { Text = label, Foreground = color, FontWeight = FontWeights.Bold, VerticalAlignment = VerticalAlignment.Center };
            
            var s = new Slider { Minimum = 0, Maximum = 1, Value = val, SmallChange = 0.01, LargeChange = 0.1, IsSnapToTickEnabled = false };
            
            var t = new TextBox { Text = val.ToString("0.##"), Style = (Style)FindResource("DarkTextBox"), FontSize = 10 };

            s.ValueChanged += (sender, e) => {
                parameters[index] = s.Value.ToString("0.###");
                t.Text = s.Value.ToString("0.##");
                onUpdate();
            };
            
            t.TextChanged += (sender, e) => {
                if (float.TryParse(t.Text, out float res)) {
                    s.Value = res;
                    parameters[index] = res.ToString(); // Allow > 1 for HDR
                    onUpdate();
                }
            };

            Grid.SetColumn(l, 0);
            Grid.SetColumn(s, 1);
            Grid.SetColumn(t, 2);
            
            g.Children.Add(l);
            g.Children.Add(s);
            g.Children.Add(t);
            
            return g;
        }

        private bool IsFloat(string s) => float.TryParse(s, out _);

        private void AddPropertyField(string label, string value, Action<string> onUpdate)
        {
            var grid = new Grid { Margin = new Thickness(0, 2, 0, 2) };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(80) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var lbl = new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center, Foreground = System.Windows.Media.Brushes.Gray, FontSize = 11 };
            var txt = new TextBox { Text = value, Style = (Style)FindResource("DarkTextBox") };
            
            txt.TextChanged += (s, e) => onUpdate(txt.Text);
            
            Grid.SetColumn(lbl, 0);
            Grid.SetColumn(txt, 1);
            
            grid.Children.Add(lbl);
            grid.Children.Add(txt);
            
            panelProperties.Children.Add(grid);
        }

        // --- Real-time Renderer Integration ---

        private Point _lastMousePos;
        private bool _isLeftMouseDown;
        private bool _isRightMouseDown;

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            var settings = new GLWpfControlSettings
            {
                MajorVersion = 3,
                MinorVersion = 3
            };
            glControl.Start(settings);
            AppendLog("GL Control Started...");
        }

        private void GlControl_Ready()
        {
            // _realtimeRenderer init logic moved here
            _realtimeRenderer = new RealtimeRenderer();
            _realtimeRenderer.Logger = AppendLog; // Connect Logger

            try {
                _realtimeRenderer.Initialize();
                AppendLog("Real-time Renderer Initialized (OpenGL 3.3)");
            } catch (Exception ex) {
                AppendLog($"Real-time Renderer Init Failed: {ex.Message}");
                MessageBox.Show($"Real-time Renderer Init Failed: {ex.Message}");
            }

            // Input Bindings
            glControl.MouseDown += GlControl_MouseDown;
            glControl.MouseUp += GlControl_MouseUp;
            glControl.MouseMove += GlControl_MouseMove;
            this.KeyDown += Window_KeyDown;

            // Force Continuous Rendering
            System.Windows.Media.CompositionTarget.Rendering += (s, e) => {
                glControl.InvalidateVisual();
            };
        }

        private void GlControl_Render(TimeSpan delta)
        {
            if (_realtimeRenderer == null) return;
            
            if (_isSceneLoading)
            {
                // Throttled log to confirm we are skipping due to loading
                 if (DateTime.Now.Second % 5 == 0 && DateTime.Now.Millisecond < 50) 
                    AppendLog("Skipping render: Scene is loading...");
                return;
            }
            
            try 
            {
                // DPI Scaling Fix
                var source = PresentationSource.FromVisual(glControl);
                double dpiX = 1.0;
                double dpiY = 1.0;
                if (source != null && source.CompositionTarget != null)
                {
                    dpiX = source.CompositionTarget.TransformToDevice.M11;
                    dpiY = source.CompositionTarget.TransformToDevice.M22;
                }
                
                int width = (int)(glControl.ActualWidth * dpiX);
                int height = (int)(glControl.ActualHeight * dpiY);
                
                if (width > 0 && height > 0)
                    _realtimeRenderer.Resize(width, height);

                // Handle Continuous Input
                HandleInput((float)delta.TotalSeconds);
                
                _realtimeRenderer.Render(_currentSceneData);
            }
            catch (Exception ex)
            {
                // Prevent crash from render loop
                if (DateTime.Now.Second % 5 == 0) 
                    AppendLog($"Render Error: {ex.Message}");
            }
        }

        private void GlControl_MouseDown(object sender, MouseButtonEventArgs e)
        {
            glControl.Focus(); // Capture Keyboard
            Keyboard.Focus(glControl); // Ensure WPF Focus
            
            if (e.ChangedButton == MouseButton.Left) _isLeftMouseDown = true;
            if (e.ChangedButton == MouseButton.Right) _isRightMouseDown = true;
            _lastMousePos = e.GetPosition(glControl);
            Mouse.Capture(glControl);
        }

        private void GlControl_MouseUp(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left) _isLeftMouseDown = false;
            if (e.ChangedButton == MouseButton.Right) _isRightMouseDown = false;
            if (!_isLeftMouseDown && !_isRightMouseDown) Mouse.Capture(null);
        }

        private void GlControl_MouseMove(object sender, MouseEventArgs e)
        {
            if (_realtimeRenderer == null) return;
            
            var currentPos = e.GetPosition(glControl);
            float xOffset = (float)(currentPos.X - _lastMousePos.X);
            float yOffset = (float)(currentPos.Y - _lastMousePos.Y);
            _lastMousePos = currentPos;

            if (_isLeftMouseDown)
            {
                _realtimeRenderer.ProcessMouse(xOffset, yOffset);
            }
            if (_isRightMouseDown)
            {
                _realtimeRenderer.ProcessPan(xOffset, yOffset);
            }
        }
        
        private void Window_KeyDown(object sender, KeyEventArgs e)
        {
             // Optional: Handle single key presses
        }

        private void GlControl_MouseEnter(object sender, MouseEventArgs e)
        {
             // Optional: Auto-focus when mouse enters to allow WASD without clicking
             // glControl.Focus(); 
        }

        private void HandleInput(float deltaTime)
        {
            if (_realtimeRenderer == null) return;
            
            // Relaxed check: Allow input if captured OR focused OR just mouse over (for convenience)
            // But we must ensure we don't steal input from textboxes.
            // Best approach: If Mouse is Captured (Dragging) OR Control has Focus.
            bool hasFocus = glControl.IsKeyboardFocused || glControl.IsFocused;
            bool isCaptured = glControl.IsMouseCaptured;
            
            // Allow WASD if we are dragging OR if we clicked the control (Focus)
            if (!hasFocus && !isCaptured) return;
            
            // Safety clamp for deltaTime to prevent huge jumps
            if (deltaTime > 0.1f) deltaTime = 0.1f;
            
            bool w = Keyboard.IsKeyDown(Key.W);
            bool s = Keyboard.IsKeyDown(Key.S);
            bool a = Keyboard.IsKeyDown(Key.A);
            bool d = Keyboard.IsKeyDown(Key.D);
            bool q = Keyboard.IsKeyDown(Key.Q); // Up
            bool e = Keyboard.IsKeyDown(Key.E); // Down
            
            if (w || s || a || d || q || e)
            {
                _realtimeRenderer.ProcessKeyboard(w, s, a, d, q, e, deltaTime);
            }
        }



        private void MenuItem_Exit_Click(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void MainWindow_Closing(object? sender, System.ComponentModel.CancelEventArgs e)
        {
            if (!string.IsNullOrEmpty(_currentProjectFile))
            {
                SaveProject(_currentProjectFile); // Auto-save project state
            }

            if (_currentProcess != null && !_currentProcess.HasExited)
            {
                try 
                { 
                    _currentProcess.Kill(); 
                    _currentProcess.Dispose();
                } 
                catch { }
            }
        }
        
        private void TxtCommandInput_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter)
            {
                string cmd = txtCommandInput.Text.Trim();
                if (!string.IsNullOrEmpty(cmd))
                {
                    AppendLog($"> {cmd}");
                    ProcessCommand(cmd);
                    txtCommandInput.Text = "";
                }
            }
        }
        
        private void ProcessCommand(string cmd)
        {
            // Simple command parser for future extensibility
            if (cmd.ToLower() == "cls" || cmd.ToLower() == "clear")
            {
                txtConsole.Text = "";
            }
            else
            {
                AppendLog("Unknown command.");
            }
        }

        private void UpdateTimer_Tick(object? sender, EventArgs e)
        {
            if (string.IsNullOrEmpty(_currentOutputPath) || !File.Exists(_currentOutputPath)) return;

            try
            {
                var fileInfo = new FileInfo(_currentOutputPath);
                if (fileInfo.LastWriteTime > _lastFileTime)
                {
                    _lastFileTime = fileInfo.LastWriteTime;
                    LoadImage(_currentOutputPath);
                }
            }
            catch (Exception)
            {
                // Ignore errors during polling (file might be locked)
            }
        }

        private void TreeAssets_SelectedItemChanged(object sender, RoutedPropertyChangedEventArgs<object> e)
        {
            if (e.NewValue is TreeViewItem item && item.Tag is string subDir)
            {
                RefreshAssetList(subDir);
            }
        }

        private void RefreshAssetList(string subDir)
        {
            listAssets.ItemsSource = null;
            string root = FindProjectRoot();
            if (string.IsNullOrEmpty(root)) return;

            string dirPath = Path.Combine(root, subDir);
            if (!Directory.Exists(dirPath))
            {
                try { Directory.CreateDirectory(dirPath); } catch { }
                return;
            }

            var files = new System.Collections.Generic.List<AssetFile>();
            try
            {
                foreach (var file in Directory.GetFiles(dirPath))
                {
                    files.Add(new AssetFile 
                    { 
                        Name = Path.GetFileName(file), 
                        FullPath = file 
                    });
                }
            }
            catch (Exception ex)
            {
                AppendLog($"Error listing assets: {ex.Message}");
            }

            listAssets.ItemsSource = files;
        }

        private void ListAssets_MouseDoubleClick(object sender, MouseButtonEventArgs e)
        {
            if (listAssets.SelectedItem is AssetFile file)
            {
                AppendLog($"[DEBUG] Double-clicked asset: {file.FullPath}");
                if (file.FullPath.EndsWith(".scene", StringComparison.OrdinalIgnoreCase))
                {
                    _currentScenePath = file.FullPath;
                    LoadSceneData(file.FullPath);
                }
                else
                {
                    AppendLog($"Selected asset: {file.Name}");
                }
            }
        }

        private async void BtnRender_Click(object sender, RoutedEventArgs e)
        {
            // 1. Validate Inputs
            if (string.IsNullOrEmpty(_currentProjectRoot))
            {
                MessageBox.Show("Please create or open a project first.", "No Project Open", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            if (string.IsNullOrEmpty(_currentScenePath))
            {
                MessageBox.Show("Please select a scene to render first.", "No Scene Selected", MessageBoxButton.OK, MessageBoxImage.Warning);
                return;
            }

            // Auto-save scene changes before rendering
            if (_currentSceneData != null && !string.IsNullOrEmpty(_currentScenePath))
            {
                try
                {
                    _currentSceneData.Save(_currentScenePath);
                    AppendLog("Scene saved before rendering.");
                }
                catch (Exception ex)
                {
                    AppendLog($"Warning: Failed to auto-save scene: {ex.Message}");
                }
            }

            if (!int.TryParse(txtWidth.Text, out int width) ||
                !int.TryParse(txtHeight.Text, out int height) ||
                !int.TryParse(txtSPP.Text, out int spp))
            {
                MessageBox.Show("Please enter valid numeric values.");
                return;
            }

            btnRender.IsEnabled = false;
            statusText.Text = "RENDERING...";
            AppendLog("Starting render...");

            try
            {
                // 2. Resolve Paths
                // BaseDirectory: E:\Render Engine\gui\UltraRender.GUI\bin\Debug\net10.0-windows\
                // We want to get to E:\Render Engine\
                string baseDir = AppDomain.CurrentDomain.BaseDirectory;
                DirectoryInfo rootDir = new DirectoryInfo(baseDir);
                // Traverse up 5 levels to get to project root
                for (int i = 0; i < 5; i++)
                {
                    if (rootDir.Parent != null) rootDir = rootDir.Parent;
                }

                string exePath = Path.Combine(rootDir.FullName, "build_gui", "bin", "Debug", "UltraRender.exe");
                string imagePath = Path.Combine(rootDir.FullName, "output", "output_procedural.bmp");
                _currentOutputPath = imagePath; // Set BEFORE process starts for progressive updates
                
                if (!File.Exists(exePath))
                {
                    AppendLog($"Error: Executable not found at {exePath}");
                    btnRender.IsEnabled = true;
                    statusText.Text = "ERROR";
                    return;
                }

                // 3. Configure Process
                string args = $"--width {width} --height {height} --spp {spp} --output output_procedural.bmp";
                
                // Add scene argument if specified
                string scenePath = _currentScenePath;
                if (!string.IsNullOrEmpty(scenePath) && File.Exists(scenePath))
                {
                    args += $" --scene \"{scenePath}\"";
                }

                ProcessStartInfo startInfo = new ProcessStartInfo
                {
                    FileName = exePath,
                    Arguments = args,
                    WorkingDirectory = rootDir.FullName, // Run from root so output goes to root/output
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    UseShellExecute = false,
                    CreateNoWindow = true
                };

                // 4. Run Async
                await Task.Run(() =>
                {
                    using (Process proc = new Process())
                    {
                        _currentProcess = proc;
                        proc.StartInfo = startInfo;
                        proc.OutputDataReceived += (s, args) => AppendLog(args.Data ?? "");
                        proc.ErrorDataReceived += (s, args) => AppendLog(args.Data ?? "");
                        
                        proc.Start();
                        proc.BeginOutputReadLine();
                        proc.BeginErrorReadLine();
                        proc.WaitForExit();
                        _currentProcess = null;
                    }
                });

                // 5. Load Result Image (Final Check)
                if (File.Exists(imagePath))
                {
                    AppendLog($"Render finished. Loading image: {imagePath}");
                    LoadImage(imagePath);
                }
                else
                {
                    AppendLog($"Error: Output image not found at {imagePath}");
                }
                statusText.Text = "FINISHED";
            }
            catch (Exception ex)
            {
                AppendLog($"Exception: {ex.Message}");
                statusText.Text = "ERROR";
            }
            finally
            {
                btnRender.IsEnabled = true;
                if (statusText.Text == "RENDERING...") statusText.Text = "READY";
            }
        }

        public void AppendLog(string message)
        {
            if (string.IsNullOrEmpty(message)) return;
            
            // 1. Write to file IMMEDIATELY (thread-safe lock)
            try 
            {
                lock (_debugLogPath)
                {
                    File.AppendAllText(_debugLogPath, $"[{DateTime.Now:HH:mm:ss.fff}] {message}\n");
                }
            }
            catch {}

            // 2. Update UI asynchronously
            // Use BeginInvoke to avoid blocking the render thread or causing re-entrancy issues
            Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.ContextIdle, new Action(() =>
            {
                txtConsole.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}\n");
                txtConsole.ScrollToEnd();
            }));
        }

        private void LoadImage(string path)
        {
            try
            {
                // Read file into memory to avoid locking and ensure fresh content
                byte[] buffer;
                
                // Retry mechanism to avoid locking issues
                int retries = 3;
                while (retries > 0)
                {
                    try 
                    {
                        using (var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                        {
                            if (fs.Length == 0) return; // Skip empty files
                            buffer = new byte[fs.Length];
                            fs.ReadExactly(buffer);
                        }
                        
                        Application.Current.Dispatcher.BeginInvoke(System.Windows.Threading.DispatcherPriority.Background, new Action(() =>
                        {
                            try {
                                using (var stream = new MemoryStream(buffer))
                                {
                                    BitmapImage bitmap = new BitmapImage();
                                    bitmap.BeginInit();
                                    bitmap.CacheOption = BitmapCacheOption.OnLoad;
                                    // bitmap.CreateOptions = BitmapCreateOptions.IgnoreImageCache; // Removed: Causes ArgumentNullException with StreamSource
                                    bitmap.StreamSource = stream;
                                    bitmap.EndInit();
                                    bitmap.Freeze(); // Make it cross-thread accessible if needed (though we are on UI thread)
                                    imgPreview.Source = bitmap;
                                }
                            } catch (Exception ex) { 
                                AppendLog($"[Preview] Decode failed: {ex.Message}");
                            }
                        }));
                        break; // Success
                    }
                    catch (IOException) 
                    {
                        retries--;
                        Thread.Sleep(100);
                    }
                }
            }
            catch (Exception ex)
            {
                AppendLog($"Error loading image: {ex.Message}");
            }
        }

        private void TabControl_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            // Placeholder or logic for other tabs if needed
        }
    }

    public class AssetFile
    {
        public string Name { get; set; } = "";
        public string FullPath { get; set; } = "";
        public string Icon { get; set; } = "📄";
        public System.Windows.Media.Brush Color { get; set; } = System.Windows.Media.Brushes.Gray;
    }

    public class ProjectConfig
    {
        public string Name { get; set; } = "Untitled";
        public string Version { get; set; } = "1.0";
        public string LastScene { get; set; } = "";
        public double WindowWidth { get; set; } = 1280;
        public double WindowHeight { get; set; } = 720;
        public bool WindowMaximized { get; set; } = false;
    }
}
