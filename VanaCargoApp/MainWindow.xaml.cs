using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using VanaCargoBridge;

namespace VanaCargoApp
{
    public partial class MainWindow : Window
    {
        private readonly CoreBridge _bridge = new CoreBridge();
        private readonly IconConverter _iconConverter = new IconConverter();
        private readonly ImageSource _tabIcon;
        private const double NormalRowHeight = 28.0;
        private const double CompactRowHeight = 20.0;
        private const double NormalFontSize = 14.0;
        private const double CompactFontSize = 11.0;
        private const string DarkModeKey = "DarkMode";
        private LoadResult _loadResult;
        private ManagedTab[] _currentTabs;
        private string _configPath;
        private bool _darkMode;

        public MainWindow()
        {
            InitializeComponent();
            _tabIcon = LoadTabIcon();
        }

        private void OnLoaded(object sender, RoutedEventArgs e)
        {
            _configPath = FindConfigPath();
            var configWasMissing = !File.Exists(_configPath);
            _loadResult = _bridge.LoadConfigAndCharacters(_configPath);
            if (_loadResult == null)
            {
                MessageBox.Show(this, "Failed to load configuration.", "VanaCargo",
                    MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }

            EnsureFindAllPaths();
            _darkMode = LoadBoolSetting(DarkModeKey, false);
            DarkModeMenu.IsChecked = _darkMode;
            App.ApplyTheme(_darkMode);
            CharactersList.ItemsSource = _loadResult.Characters;
            ApplyCompactList(_loadResult.Settings.CompactList);
            StatusText.Text = $"Loaded {_loadResult.Characters.Length} characters.";
            if (_loadResult.Characters.Length > 0)
                CharactersList.SelectedIndex = 0;
        }

        private void OnAboutClick(object sender, RoutedEventArgs e)
        {
            var win = new AboutWindow
            {
                Owner = this
            };
            win.ShowDialog();
        }

        private void OnCharacterSelected(object sender, SelectionChangedEventArgs e)
        {
            if (_loadResult == null)
                return;

            EnsureFindAllPaths();

            var character = CharactersList.SelectedItem as ManagedCharacter;
            if (character == null)
                return;

            var tabs = _bridge.LoadInventoryForCharacter(_loadResult.Settings, character, _loadResult.Tabs);
            if (tabs == null)
            {
                MessageBox.Show(this, "Failed to load inventory.", "VanaCargo",
                    MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }

            _currentTabs = tabs;
            PopulateTabs(_currentTabs);
            StatusText.Text = $"Loaded inventory for {character.Name}.";
        }

        private void PopulateTabs(ManagedTab[] tabs)
        {
            InventoryTabs.Items.Clear();
            foreach (var tab in tabs)
            {
                var isKeyItems = string.Equals(tab?.Info?.DisplayName, "Key Items", StringComparison.OrdinalIgnoreCase);
                var grid = CreateItemsGrid(isKeyItems);
                grid.ItemsSource = tab.Items;

                var tabItem = new TabItem
                {
                    Header = CreateTabHeader(tab.Info.DisplayName),
                    Content = grid
                };
                InventoryTabs.Items.Add(tabItem);
            }
        }

        private void OnDarkModeToggle(object sender, RoutedEventArgs e)
        {
            _darkMode = DarkModeMenu.IsChecked == true;
            App.ApplyTheme(_darkMode);
            SaveConfigValue(DarkModeKey, _darkMode ? "1" : "0");
        }

        private void OnGridDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            var grid = sender as DataGrid;
            if (grid == null)
                return;

            var cell = FindParentCell(e.OriginalSource as DependencyObject);
            if (cell == null)
                return;

            var column = cell.Column as DataGridTextColumn;
            var header = cell.Column?.Header as string;
            var binding = column?.Binding as Binding;
            var bindingPath = binding?.Path?.Path;
            if (!string.Equals(bindingPath, "Name", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(header, "Name", StringComparison.OrdinalIgnoreCase))
                return;

            var item = grid.SelectedItem as ManagedItem;
            if (item == null)
                return;

            OpenBgWikiForItem(item);
        }

        private void OnOpenBgWiki(object sender, RoutedEventArgs e)
        {
            var menuItem = sender as MenuItem;
            var grid = menuItem?.Tag as DataGrid;
            var item = grid?.SelectedItem as ManagedItem;
            if (item == null)
                return;

            OpenBgWikiForItem(item);
        }

        private void OpenBgWikiForItem(ManagedItem item)
        {
            var url = BuildBgWikiUrl(item.Name);
            try
            {
                Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
                StatusText.Text = $"Opened BG Wiki for {item.Name}.";
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Failed to open BG Wiki.\n" + ex.Message, "VanaCargo",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void OnExport(object sender, RoutedEventArgs e)
        {
            if (_loadResult == null)
                return;

            var exportDialog = new ExportDialog(_loadResult.Characters)
            {
                Owner = this
            };

            if (exportDialog.ShowDialog() != true)
                return;

            var selectedChars = exportDialog.SelectedCharacters;
            var selectedColumns = exportDialog.SelectedColumns;
            if (selectedChars.Count == 0 || selectedColumns.Count == 0)
            {
                MessageBox.Show(this, "Nothing to export.", "VanaCargo",
                    MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            var saveDialog = new SaveFileDialog
            {
                Filter = "CSV files (*.csv)|*.csv",
                FileName = "export.csv",
                OverwritePrompt = true
            };

            if (saveDialog.ShowDialog(this) != true)
                return;

            var includeBg = selectedColumns.Any(c => c.Key == "BgWiki");
            var includeCount = selectedColumns.Any(c => c.Key == "Count");
            var includeName = selectedColumns.Any(c => c.Key == "Name");
            var includeAttr = selectedColumns.Any(c => c.Key == "Attr");
            var includeDesc = selectedColumns.Any(c => c.Key == "Description");
            var includeType = selectedColumns.Any(c => c.Key == "Type");
            var includeRaces = selectedColumns.Any(c => c.Key == "Races");
            var includeLevel = selectedColumns.Any(c => c.Key == "Level");
            var includeJobs = selectedColumns.Any(c => c.Key == "Jobs");
            var includeRemarks = selectedColumns.Any(c => c.Key == "Remarks");

            var progress = new ExportProgressWindow { Owner = this };
            progress.Show();

            try
            {
                var rows = await Task.Run(() => LoadExportRows(selectedChars, progress));
                if (rows == null)
                {
                    StatusText.Text = "Export canceled.";
                    return;
                }

                var totalRows = rows.Count;
                progress.UpdateStatus("Writing export file...", 0, totalRows);

                await Task.Run(() =>
                {
                    using (var writer = new StreamWriter(saveDialog.FileName, false, new System.Text.UTF8Encoding(true)))
                    {
                        var header = new List<string> { "Character", "Location" };
                        if (includeName) header.Add("Name");
                        if (includeAttr) header.Add("Attributes");
                        if (includeDesc) header.Add("Description");
                        if (includeType) header.Add("Type");
                        if (includeRaces) header.Add("Races");
                        if (includeLevel) header.Add("Level");
                        if (includeJobs) header.Add("Jobs");
                        if (includeRemarks) header.Add("Remarks");
                        if (includeBg) header.Add("BG Wiki URL");
                        if (includeCount) header.Add("Count");
                        
                        WriteCsvRow(writer, header);

                        for (var i = 0; i < totalRows; i++)
                        {
                            if (progress.IsCanceled)
                                return;

                            var row = rows[i];
                            var cols = new List<string>
                            {
                                row.Character,
                                row.Location
                            };

                            if (includeName) cols.Add(row.Item.Name);
                            if (includeAttr) cols.Add(row.Item.Attr);
                            if (includeDesc) cols.Add(row.Item.Description);
                            if (includeType) cols.Add(row.Item.Slot);
                            if (includeRaces) cols.Add(row.Item.Races);
                            if (includeLevel) cols.Add(row.Item.Level);
                            if (includeJobs) cols.Add(row.Item.Jobs);
                            if (includeRemarks) cols.Add(row.Item.Remarks);
                            if (includeBg) cols.Add(BuildBgWikiUrl(row.Item.Name));
                            if (includeCount) cols.Add(row.Item.Count.ToString(CultureInfo.InvariantCulture));

                            WriteCsvRow(writer, cols);
                            if (i % 50 == 0)
                            {
                                progress.Dispatcher.Invoke(() =>
                                    progress.UpdateStatus($"Writing export file... {i + 1}/{totalRows}", i + 1, totalRows));
                            }
                        }
                    }
                });

                if (progress.IsCanceled)
                {
                    StatusText.Text = "Export canceled.";
                    return;
                }

                StatusText.Text = "Export completed.";
                Process.Start(new ProcessStartInfo(saveDialog.FileName) { UseShellExecute = true });
            }
            finally
            {
                progress.Close();
            }
        }

        private void OnQuit(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private sealed class ExportRow
        {
            public string Character { get; set; }
            public string Location { get; set; }
            public ManagedItem Item { get; set; }
        }

        private List<ExportRow> LoadExportRows(IReadOnlyList<ExportDialog.CharacterOption> selectedChars, ExportProgressWindow progress)
        {
            var rows = new List<ExportRow>();
            for (var i = 0; i < selectedChars.Count; i++)
            {
                if (progress.IsCanceled)
                    return null;

                var characterOption = selectedChars[i];
                progress.Dispatcher.Invoke(() => progress.UpdateStatus($"Loading inventories... {i + 1}/{selectedChars.Count}", i + 1, selectedChars.Count));

                var character = _loadResult.Characters.FirstOrDefault(c => c.Id == characterOption.Id);
                if (character == null)
                    continue;

                var tabs = _bridge.LoadInventoryForCharacter(_loadResult.Settings, character, _loadResult.Tabs);
                if (tabs == null)
                    continue;

                foreach (var tab in tabs)
                {
                    if (tab.Items == null)
                        continue;

                    foreach (var item in tab.Items)
                    {
                        rows.Add(new ExportRow
                        {
                            Character = character.Name,
                            Location = tab.Info.DisplayName,
                            Item = item
                        });
                    }
                }
            }

            return rows;
        }

        private static void WriteCsvRow(StreamWriter writer, IReadOnlyList<string> values)
        {
            for (var i = 0; i < values.Count; i++)
            {
                if (i > 0)
                    writer.Write(",");

                writer.Write(EscapeCsv(values[i]));
            }
            writer.WriteLine();
        }

        private static string EscapeCsv(string value)
        {
            if (string.IsNullOrEmpty(value))
                return string.Empty;

            var needsQuotes = value.IndexOfAny(new[] { ',', '"', '\n', '\r' }) >= 0;
            var escaped = value.Replace("\"", "\"\"");
            return needsQuotes ? $"\"{escaped}\"" : escaped;
        }

        private DataGrid CreateItemsGrid(bool isKeyItems = false)
        {
            var noWrapStyle = new Style(typeof(TextBlock));
            noWrapStyle.Setters.Add(new Setter(TextBlock.TextWrappingProperty, TextWrapping.NoWrap));
            noWrapStyle.Setters.Add(new Setter(TextBlock.TextTrimmingProperty, TextTrimming.CharacterEllipsis));
            noWrapStyle.Setters.Add(new Setter(TextBlock.VerticalAlignmentProperty, VerticalAlignment.Center));

            var grid = new DataGrid
            {
                AutoGenerateColumns = false,
                CanUserAddRows = false,
                IsReadOnly = true,
                RowHeight = NormalRowHeight,
                FontSize = NormalFontSize,
                HeadersVisibility = DataGridHeadersVisibility.Column,
                RowHeaderWidth = 0,
                BorderThickness = new Thickness(0),
                GridLinesVisibility = DataGridGridLinesVisibility.All
            };

            grid.MouseDoubleClick += OnGridDoubleClick;
            grid.ContextMenu = CreateGridContextMenu(grid);

            var iconColumn = new DataGridTemplateColumn
            {
                Header = string.Empty,
                Width = 28,
                IsReadOnly = true
            };

            var imageFactory = new FrameworkElementFactory(typeof(Image));
            imageFactory.SetValue(Image.WidthProperty, 16.0);
            imageFactory.SetValue(Image.HeightProperty, 16.0);
            imageFactory.SetValue(Image.MarginProperty, new Thickness(2, 0, 2, 0));
            imageFactory.SetValue(RenderOptions.BitmapScalingModeProperty, BitmapScalingMode.NearestNeighbor);
            imageFactory.SetValue(UIElement.SnapsToDevicePixelsProperty, true);

            var iconBinding = new MultiBinding { Converter = _iconConverter };
            iconBinding.Bindings.Add(new Binding("IconPixels"));
            iconBinding.Bindings.Add(new Binding("IconWidth"));
            iconBinding.Bindings.Add(new Binding("IconHeight"));
            iconBinding.Bindings.Add(new Binding("IconStride"));

            imageFactory.SetBinding(Image.SourceProperty, iconBinding);
            iconColumn.CellTemplate = new DataTemplate { VisualTree = imageFactory };
            grid.Columns.Add(iconColumn);

            grid.Columns.Add(new DataGridTextColumn { Header = "Count", Binding = new Binding("Count"), ElementStyle = noWrapStyle });
            grid.Columns.Add(new DataGridTextColumn { Header = "Name", Binding = new Binding("Name"), ElementStyle = noWrapStyle });

            if (isKeyItems)
            {
                grid.Columns.Add(new DataGridTextColumn { Header = "Type", Binding = new Binding("Slot"), ElementStyle = noWrapStyle });
                grid.Columns.Add(new DataGridTextColumn { Header = "Category", Binding = new Binding("Remarks"), ElementStyle = noWrapStyle });
            }
            else
            {
                grid.Columns.Add(new DataGridTextColumn { Header = "Attr", Binding = new Binding("Attr"), ElementStyle = noWrapStyle });
                grid.Columns.Add(new DataGridTextColumn { Header = "Description", Binding = new Binding("Description"), ElementStyle = noWrapStyle });
                grid.Columns.Add(new DataGridTextColumn { Header = "Type", Binding = new Binding("Slot"), ElementStyle = noWrapStyle });
                grid.Columns.Add(new DataGridTextColumn { Header = "Races", Binding = new Binding("Races"), ElementStyle = noWrapStyle });
                grid.Columns.Add(new DataGridTextColumn { Header = "Level", Binding = new Binding("Level"), ElementStyle = noWrapStyle });
                grid.Columns.Add(new DataGridTextColumn { Header = "Jobs", Binding = new Binding("Jobs"), ElementStyle = noWrapStyle });
                grid.Columns.Add(new DataGridTextColumn { Header = "Remarks", Binding = new Binding("Remarks"), ElementStyle = noWrapStyle });
            }

            return grid;
        }

        private object CreateTabHeader(string title)
        {
            if (_tabIcon == null)
                return title ?? string.Empty;

            var panel = new StackPanel
            {
                Orientation = Orientation.Horizontal
            };

            panel.Children.Add(new Image
            {
                Source = _tabIcon,
                Width = 16,
                Height = 16,
                Margin = new Thickness(2, 0, 4, 0),
                SnapsToDevicePixels = true
            });

            panel.Children.Add(new TextBlock
            {
                Text = title ?? string.Empty,
                VerticalAlignment = VerticalAlignment.Center
            });

            return panel;
        }

        private static ImageSource LoadTabIcon()
        {
            try
            {
                var bitmap = new BitmapImage();
                bitmap.BeginInit();
                bitmap.UriSource = new Uri("pack://application:,,,/Assets/safe16alpha.ico", UriKind.Absolute);
                bitmap.CacheOption = BitmapCacheOption.OnLoad;
                bitmap.EndInit();
                bitmap.Freeze();
                return bitmap;
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Failed to load tab icon: {ex.Message}");
                return null;
            }
        }

        private void ApplyCompactList(bool compact)
        {
            var rowHeight = compact ? CompactRowHeight : NormalRowHeight;
            var fontSize = compact ? CompactFontSize : NormalFontSize;

            foreach (var tabItemObj in InventoryTabs.Items)
            {
                if (tabItemObj is TabItem tabItem && tabItem.Content is DataGrid grid)
                {
                    grid.RowHeight = rowHeight;
                    grid.FontSize = fontSize;
                }
            }
        }

        private void OnOpenSearch(object sender, RoutedEventArgs e)
        {
            if (_loadResult == null)
                return;

            var window = new SearchWindow(_bridge, _loadResult)
            {
                Owner = this
            };
            window.Show();
        }

        private void OnOpenConfig(object sender, RoutedEventArgs e)
        {
            if (_loadResult?.Settings == null)
                return;

            var window = new SettingsWindow(_loadResult.Settings, _loadResult.Characters)
            {
                Owner = this
            };

            if (window.ShowDialog() != true)
                return;

            var selectedId = (CharactersList.SelectedItem as ManagedCharacter)?.Id;
            var prevFindAll = _loadResult.Settings.FindAllEnabled;

            _loadResult.Settings.FindAllEnabled = window.FindAllEnabled;
            _loadResult.Settings.FindAllDataPath = window.FindAllDataPath;
            _loadResult.Settings.FindAllKeyItemsPath = window.FindAllKeyItemsPath;
            _bridge.SaveSettings(_configPath, _loadResult.Settings);
            SaveCharacterDisplayNames(window.Characters);

            if (prevFindAll != window.FindAllEnabled)
                ReloadConfigAndCharacters(selectedId);
            else
                RefreshCharacterList(window.Characters);
        }

        private void SaveCharacterDisplayNames(IReadOnlyList<SettingsWindow.CharacterOption> characters)
        {
            if (characters == null)
                return;

            var managed = characters
                .Select(c => new ManagedCharacter
                {
                    Id = c.Id,
                    Name = c.Name
                })
                .ToArray();

            _bridge.SaveCharacterDisplayNames(_configPath, managed);
        }

        private void RefreshCharacterList(IReadOnlyList<SettingsWindow.CharacterOption> characters)
        {
            if (characters == null || _loadResult?.Characters == null)
                return;

            foreach (var option in characters)
            {
                var match = _loadResult.Characters.FirstOrDefault(c => c.Id == option.Id);
                if (match != null)
                    match.Name = string.IsNullOrEmpty(option.Name) ? option.Id : option.Name;
            }

            CharactersList.Items.Refresh();
        }

        private void ReloadConfigAndCharacters(string selectedId)
        {
            var result = _bridge.LoadConfigAndCharacters(_configPath);
            if (result == null)
                return;

            _loadResult = result;
            EnsureFindAllPaths();
            CharactersList.ItemsSource = _loadResult.Characters;

            if (!string.IsNullOrEmpty(selectedId))
            {
                var match = _loadResult.Characters.FirstOrDefault(c => c.Id == selectedId);
                if (match != null)
                    CharactersList.SelectedItem = match;
            }
        }

        private void EnsureFindAllPaths()
        {
            if (_loadResult?.Settings == null || string.IsNullOrWhiteSpace(_configPath))
                return;

            var dataPath = string.Empty;
            var keyItemsPath = string.Empty;
            var inConfig = false;

            if (File.Exists(_configPath))
            {
                foreach (var rawLine in File.ReadLines(_configPath))
                {
                    var line = rawLine.Trim();
                    if (line.Length == 0 || line.StartsWith(";", StringComparison.Ordinal) || line.StartsWith("#", StringComparison.Ordinal))
                        continue;

                    if (line.StartsWith("[", StringComparison.Ordinal) && line.EndsWith("]", StringComparison.Ordinal))
                    {
                        var section = line.Substring(1, line.Length - 2);
                        inConfig = string.Equals(section, "Config", StringComparison.OrdinalIgnoreCase);
                        continue;
                    }

                    if (!inConfig)
                        continue;

                    var eq = line.IndexOf('=');
                    if (eq < 0)
                        continue;

                    var key = line.Substring(0, eq).Trim();
                    var value = line.Substring(eq + 1).Trim();

                    if (string.Equals(key, "FindAllDataPath", StringComparison.OrdinalIgnoreCase))
                        dataPath = value;
                    else if (string.Equals(key, "FindAllKeyItemsPath", StringComparison.OrdinalIgnoreCase))
                        keyItemsPath = value;
                }
            }

            if (!string.IsNullOrWhiteSpace(dataPath))
                _loadResult.Settings.FindAllDataPath = dataPath;

            if (!string.IsNullOrWhiteSpace(keyItemsPath))
                _loadResult.Settings.FindAllKeyItemsPath = keyItemsPath;

            if (string.IsNullOrWhiteSpace(_loadResult.Settings.FindAllDataPath) ||
                string.IsNullOrWhiteSpace(_loadResult.Settings.FindAllKeyItemsPath))
            {
                var baseDir = Path.GetDirectoryName(_configPath) ?? string.Empty;
                if (string.IsNullOrWhiteSpace(_loadResult.Settings.FindAllDataPath))
                    _loadResult.Settings.FindAllDataPath = Path.Combine(baseDir, "addons", "findAll", "data");
                if (string.IsNullOrWhiteSpace(_loadResult.Settings.FindAllKeyItemsPath))
                    _loadResult.Settings.FindAllKeyItemsPath = Path.Combine(baseDir, "res", "key_items.lua");
            }
        }

        private ContextMenu CreateGridContextMenu(DataGrid grid)
        {
            var menu = new ContextMenu();
            var openBg = new MenuItem { Header = "Open BG Wiki", Tag = grid };
            openBg.Click += OnOpenBgWiki;
            menu.Items.Add(openBg);
            return menu;
        }

        private static DataGridCell FindParentCell(DependencyObject source)
        {
            var current = source;
            while (current != null && !(current is DataGridCell))
                current = VisualTreeHelper.GetParent(current);

            return current as DataGridCell;
        }

        private static string BuildBgWikiUrl(string itemName)
        {
            if (string.IsNullOrEmpty(itemName))
                return "https://www.bg-wiki.com/ffxi/";

            var escaped = Uri.EscapeDataString(itemName);
            return "https://www.bg-wiki.com/ffxi/" + escaped;
        }

        private static string FindConfigPath()
        {
            var baseDir = AppDomain.CurrentDomain.BaseDirectory;
            var localConfig = Path.Combine(baseDir, "config.ini");
            if (File.Exists(localConfig))
                return localConfig;

            var dir = new DirectoryInfo(baseDir);
            while (dir != null)
            {
                var slnPath = Path.Combine(dir.FullName, "VanaCargo.sln");
                if (File.Exists(slnPath))
                {
                    var releaseConfig = Path.Combine(dir.FullName, "_Release", "config.ini");
                    if (File.Exists(releaseConfig))
                        return releaseConfig;

                    var rootConfig = Path.Combine(dir.FullName, "config.ini");
                    if (File.Exists(rootConfig))
                        return rootConfig;

                    break;
                }

                dir = dir.Parent;
            }

            return localConfig;
        }

        private bool LoadBoolSetting(string key, bool defaultValue)
        {
            if (string.IsNullOrWhiteSpace(_configPath) || !File.Exists(_configPath))
                return defaultValue;

            bool inConfig = false;
            foreach (var raw in File.ReadAllLines(_configPath))
            {
                var line = raw.Trim();
                if (line.Length == 0 || line.StartsWith(";") || line.StartsWith("#"))
                    continue;

                if (line.StartsWith("[") && line.EndsWith("]"))
                {
                    var section = line.Substring(1, line.Length - 2).Trim();
                    inConfig = string.Equals(section, "Config", StringComparison.OrdinalIgnoreCase);
                    continue;
                }

                if (!inConfig)
                    continue;

                var eq = line.IndexOf('=');
                if (eq <= 0)
                    continue;

                var name = line.Substring(0, eq).Trim();
                if (!string.Equals(name, key, StringComparison.OrdinalIgnoreCase))
                    continue;

                var value = line.Substring(eq + 1).Trim();
                return IsTruthy(value);
            }

            return defaultValue;
        }

        private void SaveConfigValue(string key, string value)
        {
            if (string.IsNullOrWhiteSpace(_configPath))
                return;

            var lines = File.Exists(_configPath)
                ? File.ReadAllLines(_configPath).ToList()
                : new List<string>();

            var sectionStart = -1;
            var sectionEnd = lines.Count;
            for (var i = 0; i < lines.Count; i++)
            {
                var line = lines[i].Trim();
                if (!line.StartsWith("[") || !line.EndsWith("]"))
                    continue;

                var section = line.Substring(1, line.Length - 2).Trim();
                if (sectionStart >= 0)
                {
                    sectionEnd = i;
                    break;
                }

                if (string.Equals(section, "Config", StringComparison.OrdinalIgnoreCase))
                    sectionStart = i;
            }

            if (sectionStart < 0)
            {
                lines.Add("[Config]");
                lines.Add($"{key} = {value}");
                File.WriteAllLines(_configPath, lines);
                return;
            }

            bool updated = false;
            for (var i = sectionStart + 1; i < sectionEnd; i++)
            {
                var line = lines[i].Trim();
                if (line.Length == 0 || line.StartsWith(";") || line.StartsWith("#"))
                    continue;

                var eq = line.IndexOf('=');
                if (eq <= 0)
                    continue;

                var name = line.Substring(0, eq).Trim();
                if (!string.Equals(name, key, StringComparison.OrdinalIgnoreCase))
                    continue;

                lines[i] = $"{name} = {value}";
                updated = true;
                break;
            }

            if (!updated)
                lines.Insert(sectionEnd, $"{key} = {value}");

            File.WriteAllLines(_configPath, lines);
        }

        private static bool IsTruthy(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return false;

            var normalized = value.Trim();
            return normalized == "1" ||
                   normalized.Equals("true", StringComparison.OrdinalIgnoreCase) ||
                   normalized.Equals("yes", StringComparison.OrdinalIgnoreCase) ||
                   normalized.Equals("on", StringComparison.OrdinalIgnoreCase);
        }
    }
}
