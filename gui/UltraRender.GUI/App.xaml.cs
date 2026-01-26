using System.Configuration;
using System.Data;
using System.Windows;
using System.IO;

namespace UltraRender.GUI;

/// <summary>
/// Interaction logic for App.xaml
/// </summary>
public partial class App : Application
{
    private void Application_Startup(object sender, StartupEventArgs e)
    {
        // Global Exception Handling
        AppDomain.CurrentDomain.UnhandledException += CurrentDomain_UnhandledException;
        Dispatcher.UnhandledException += Dispatcher_UnhandledException;

        try
        {
            MainWindow window = new MainWindow();
            this.MainWindow = window; // Register as main window for global access
            window.Show();
        }
        catch (Exception ex)
        {
            LogFatalCrash(ex);
            Shutdown(1);
        }
    }

    private void Dispatcher_UnhandledException(object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e)
    {
        LogFatalCrash(e.Exception);
        e.Handled = true; 
        Shutdown(1);
    }

    private void CurrentDomain_UnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        if (e.ExceptionObject is Exception ex)
        {
            LogFatalCrash(ex);
        }
    }

    private void LogFatalCrash(Exception ex)
    {
        string logPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "crash_log.txt");
        
        // Try to get Project Path
        if (Application.Current.MainWindow is MainWindow mw && !string.IsNullOrEmpty(mw.CurrentProjectRoot))
        {
             logPath = Path.Combine(mw.CurrentProjectRoot, "crash_log.txt");
        }

        try 
        {
            string content = $"[{DateTime.Now}] FATAL CRASH\n" +
                             $"Message: {ex.Message}\n" +
                             $"Stack Trace:\n{ex.StackTrace}\n" +
                             $"Source: {ex.Source}\n" +
                             "--------------------------------------------------\n";
            
            File.AppendAllText(logPath, content);
            
            MessageBox.Show($"A fatal error occurred.\nLog saved to: {logPath}\n\nError: {ex.Message}", "UltraRender Crash", MessageBoxButton.OK, MessageBoxImage.Error);
        }
        catch 
        {
            MessageBox.Show($"Fatal error and failed to write log: {ex.Message}", "UltraRender Fatal", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }
}

