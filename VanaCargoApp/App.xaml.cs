using System.Windows;
using System.Windows.Media;

namespace VanaCargoApp
{
    public partial class App : Application
    {
        public static void ApplyTheme(bool darkMode)
        {
            var resources = Current?.Resources;
            if (resources == null)
                return;

            if (darkMode)
            {
                var appBg = Color.FromRgb(30, 30, 30);
                var appFg = Color.FromRgb(241, 241, 241);
                var panelBg = Color.FromRgb(37, 37, 38);
                var controlBg = Color.FromRgb(45, 45, 48);
                var controlFg = Color.FromRgb(241, 241, 241);
                var border = Color.FromRgb(60, 60, 60);
                var accent = Color.FromRgb(0, 120, 212);
                var hover = Color.FromRgb(58, 61, 65);
                var pressed = Color.FromRgb(42, 45, 46);
                var disabled = Color.FromRgb(154, 154, 154);
                var gridRow = Color.FromRgb(30, 30, 30);
                var gridAlt = Color.FromRgb(38, 38, 38);
                var gridLine = Color.FromRgb(58, 58, 58);

                SetBrush(resources, "AppBackgroundBrush", appBg);
                SetBrush(resources, "AppForegroundBrush", appFg);
                SetBrush(resources, "PanelBackgroundBrush", panelBg);
                SetBrush(resources, "ControlBackgroundBrush", controlBg);
                SetBrush(resources, "ControlForegroundBrush", controlFg);
                SetBrush(resources, "ControlBorderBrush", border);
                SetBrush(resources, "AccentBrush", accent);
                SetBrush(resources, "HoverBrush", hover);
                SetBrush(resources, "PressedBrush", pressed);
                SetBrush(resources, "DisabledForegroundBrush", disabled);
                SetBrush(resources, "DataGridRowBrush", gridRow);
                SetBrush(resources, "DataGridAltRowBrush", gridAlt);
                SetBrush(resources, "DataGridGridLineBrush", gridLine);

                SetSystemBrush(resources, SystemColors.WindowBrushKey, appBg);
                SetSystemBrush(resources, SystemColors.WindowTextBrushKey, appFg);
                SetSystemBrush(resources, SystemColors.ControlBrushKey, controlBg);
                SetSystemBrush(resources, SystemColors.ControlTextBrushKey, controlFg);
                SetSystemBrush(resources, SystemColors.MenuBrushKey, panelBg);
                SetSystemBrush(resources, SystemColors.MenuTextBrushKey, appFg);
                SetSystemBrush(resources, SystemColors.HighlightBrushKey, accent);
                SetSystemBrush(resources, SystemColors.HighlightTextBrushKey, appFg);
                SetSystemBrush(resources, SystemColors.InactiveSelectionHighlightBrushKey, hover);
                SetSystemBrush(resources, SystemColors.InactiveSelectionHighlightTextBrushKey, appFg);
            }
            else
            {
                var appBg = Colors.White;
                var appFg = Colors.Black;
                var panelBg = Color.FromRgb(245, 245, 245);
                var controlBg = Colors.White;
                var controlFg = Colors.Black;
                var border = Color.FromRgb(200, 200, 200);
                var accent = Color.FromRgb(0, 120, 212);
                var hover = Color.FromRgb(229, 241, 251);
                var pressed = Color.FromRgb(204, 228, 247);
                var disabled = Color.FromRgb(120, 120, 120);
                var gridRow = Colors.White;
                var gridAlt = Color.FromRgb(245, 245, 245);
                var gridLine = Color.FromRgb(220, 220, 220);

                SetBrush(resources, "AppBackgroundBrush", appBg);
                SetBrush(resources, "AppForegroundBrush", appFg);
                SetBrush(resources, "PanelBackgroundBrush", panelBg);
                SetBrush(resources, "ControlBackgroundBrush", controlBg);
                SetBrush(resources, "ControlForegroundBrush", controlFg);
                SetBrush(resources, "ControlBorderBrush", border);
                SetBrush(resources, "AccentBrush", accent);
                SetBrush(resources, "HoverBrush", hover);
                SetBrush(resources, "PressedBrush", pressed);
                SetBrush(resources, "DisabledForegroundBrush", disabled);
                SetBrush(resources, "DataGridRowBrush", gridRow);
                SetBrush(resources, "DataGridAltRowBrush", gridAlt);
                SetBrush(resources, "DataGridGridLineBrush", gridLine);

                SetSystemBrush(resources, SystemColors.WindowBrushKey, appBg);
                SetSystemBrush(resources, SystemColors.WindowTextBrushKey, appFg);
                SetSystemBrush(resources, SystemColors.ControlBrushKey, controlBg);
                SetSystemBrush(resources, SystemColors.ControlTextBrushKey, controlFg);
                SetSystemBrush(resources, SystemColors.MenuBrushKey, panelBg);
                SetSystemBrush(resources, SystemColors.MenuTextBrushKey, appFg);
                SetSystemBrush(resources, SystemColors.HighlightBrushKey, accent);
                SetSystemBrush(resources, SystemColors.HighlightTextBrushKey, Colors.White);
                SetSystemBrush(resources, SystemColors.InactiveSelectionHighlightBrushKey, hover);
                SetSystemBrush(resources, SystemColors.InactiveSelectionHighlightTextBrushKey, appFg);
            }
        }

        private static void SetBrush(ResourceDictionary resources, string key, Color color)
        {
            var brush = new SolidColorBrush(color);
            brush.Freeze();
            if (resources.Contains(key))
                resources[key] = brush;
            else
                resources.Add(key, brush);
        }

        private static void SetSystemBrush(ResourceDictionary resources, object key, Color color)
        {
            var brush = new SolidColorBrush(color);
            brush.Freeze();
            if (resources.Contains(key))
                resources[key] = brush;
            else
                resources.Add(key, brush);
        }
    }
}
