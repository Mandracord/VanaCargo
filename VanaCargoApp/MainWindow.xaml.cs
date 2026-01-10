using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Threading;
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
        private static readonly string[] FfxiahServers =
        {
            "Bahamut", "Shiva", "Phoenix", "Carbuncle", "Fenrir", "Sylph", "Valefor", "Leviathan",
            "Odin", "Quetzalcoatl", "Siren", "Ragnarok", "Cerberus", "Bismarck", "Lakshmi", "Asura"
        };
        private static readonly Dictionary<string, int> FfxiahServerIds =
            new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase)
            {
                { "Bahamut", 1 },
                { "Shiva", 2 },
                { "Phoenix", 5 },
                { "Carbuncle", 6 },
                { "Fenrir", 7 },
                { "Sylph", 8 },
                { "Valefor", 9 },
                { "Leviathan", 11 },
                { "Odin", 12 },
                { "Quetzalcoatl", 16 },
                { "Siren", 17 },
                { "Ragnarok", 20 },
                { "Cerberus", 23 },
                { "Bismarck", 25 },
                { "Lakshmi", 27 },
                { "Asura", 28 }
            };

        private readonly CoreBridge _bridge = new CoreBridge();
        private readonly HttpClient _httpClient = new HttpClient();
        private readonly Dictionary<int, string> _medianCache = new Dictionary<int, string>();
        private readonly Dictionary<int, string> _lastSaleCache = new Dictionary<int, string>();
        private readonly Dictionary<int, DateTimeOffset> _ffxiahCacheTimes = new Dictionary<int, DateTimeOffset>();
        private readonly IconConverter _iconConverter = new IconConverter();
        private readonly ImageSource _tabIcon;
        private const double NormalRowHeight = 28.0;
        private const double CompactRowHeight = 20.0;
        private const double NormalFontSize = 14.0;
        private const double CompactFontSize = 11.0;
        private static readonly TimeSpan CacheTtl = TimeSpan.FromHours(24);
        private const string DarkModeKey = "DarkMode";
        private LoadResult _loadResult;
        private ManagedTab[] _currentTabs;
        private CancellationTokenSource _fetchCts;
        private string _configPath;
        private string _currentServer;
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
            _currentServer = _loadResult.Settings.FfxiahServer ?? string.Empty;
            LoadMedianCache(_currentServer);
            LoadLastSaleCache(_currentServer);
            LoadCacheTimes(_currentServer);
            ApplyCompactList(_loadResult.Settings.CompactList);
            StatusText.Text = $"Loaded {_loadResult.Characters.Length} characters.";
            FetchPricesButton.IsEnabled = true;
            if (_loadResult.Characters.Length > 0)
                CharactersList.SelectedIndex = 0;

            if (configWasMissing || string.IsNullOrWhiteSpace(_currentServer))
                PromptForServer();
        }

        private void OnAboutClick(object sender, RoutedEventArgs e)
        {
            var win = new AboutWindow
            {
                Owner = this
            };
            win.ShowDialog();
        }

        private void DataGrid_AutoGeneratingColumn(object sender, DataGridAutoGeneratingColumnEventArgs e)
        {
            if (e.Column is DataGridTextColumn textColumn)
            {
                var style = new Style(typeof(TextBlock));
                style.Setters.Add(new Setter(TextBlock.TextWrappingProperty, TextWrapping.NoWrap));
                style.Setters.Add(new Setter(TextBlock.TextTrimmingProperty, TextTrimming.CharacterEllipsis));
                style.Setters.Add(new Setter(TextBlock.VerticalAlignmentProperty, VerticalAlignment.Center));

                textColumn.ElementStyle = style;
            }
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
            ApplyCachedFfxiahValues();
            RefreshGrids();
            StatusText.Text = $"Loaded inventory for {character.Name}.";
        }

        private void OnServerChanged(object sender, RoutedEventArgs e)
        {
            if (_loadResult == null)
                return;

            var nextServer = _currentServer ?? string.Empty;
            if (string.Equals(_currentServer, nextServer, StringComparison.OrdinalIgnoreCase))
                return;

            _currentServer = nextServer;
            _loadResult.Settings.FfxiahServer = _currentServer;
            _bridge.SaveSettings(_configPath, _loadResult.Settings);
            LoadMedianCache(_currentServer);
            LoadLastSaleCache(_currentServer);
            LoadCacheTimes(_currentServer);
            ApplyCachedFfxiahValues();
            RefreshGrids();
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

        private void OnOpenFfxiah(object sender, RoutedEventArgs e)
        {
            var menuItem = sender as MenuItem;
            var grid = menuItem?.Tag as DataGrid;
            var item = grid?.SelectedItem as ManagedItem;
            if (item == null)
                return;

            OpenFfxiahForItem(item);
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

        private void OpenFfxiahForItem(ManagedItem item)
        {
            if (item.Id <= 0)
            {
                MessageBox.Show(this, "This item does not have a valid ID for FFXIAH.", "VanaCargo",
                    MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            var url = BuildFfxiahUrl(item.Id, item.Name, null);
            try
            {
                Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
                StatusText.Text = $"Opened FFXIAH for {item.Name}.";
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Failed to open FFXIAH.\n" + ex.Message, "VanaCargo",
                    MessageBoxButton.OK, MessageBoxImage.Error);
            }
        }

        private async void OnFetchPrices(object sender, RoutedEventArgs e)
        {
            if (_currentTabs == null)
                return;

            var server = _currentServer?.Trim();
            if (string.IsNullOrEmpty(server))
            {
                MessageBox.Show(this, "Please select a server before fetching prices.", "VanaCargo",
                    MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            var items = _currentTabs
                .Where(tab => !string.Equals(tab.Info?.DisplayName, "Key Items", StringComparison.OrdinalIgnoreCase))
                .SelectMany(tab => tab.Items ?? Array.Empty<ManagedItem>())
                .Where(item => item.Id > 0)
                .GroupBy(item => item.Id)
                .Select(group => group.First())
                .ToList();

            if (items.Count == 0)
            {
                StatusText.Text = "No items to fetch.";
                return;
            }

            FetchPricesButton.IsEnabled = false;
            _fetchCts?.Cancel();
            _fetchCts = new CancellationTokenSource();
            var token = _fetchCts.Token;
            StatusProgress.Visibility = Visibility.Visible;
            StatusProgress.Minimum = 0;

            try
            {
                var toFetch = new List<ManagedItem>();
                foreach (var item in items)
                {
                    var cacheFresh = IsCacheFresh(item.Id);
                    string cachedMedian = null;
                    string cachedLastSale = null;
                    var hasMedian = cacheFresh && _medianCache.TryGetValue(item.Id, out cachedMedian);
                    var hasLastSale = cacheFresh && _lastSaleCache.TryGetValue(item.Id, out cachedLastSale);

                    if (hasMedian || hasLastSale)
                        UpdateFfxiahValues(item.Id, hasMedian ? cachedMedian : null, hasLastSale ? cachedLastSale : null);

                    if (!hasMedian || !hasLastSale)
                        toFetch.Add(item);
                }
                RefreshGrids();

                StatusProgress.Maximum = toFetch.Count;
                if (toFetch.Count == 0)
                {
                    StatusText.Text = "FFXIAH prices already cached.";
                    return;
                }

                for (var index = 0; index < toFetch.Count; index++)
                {
                    token.ThrowIfCancellationRequested();
                    var item = toFetch[index];
                    var values = await GetFfxiahValuesForItemAsync(item.Id, item.Name, server, token).ConfigureAwait(true);
                    UpdateFfxiahValues(item.Id, values.Median, values.LastSale);
                    RefreshGrids();

                    UpdateStatusProgress(index + 1, toFetch.Count);
                }

                SaveMedianCache(server);
                SaveLastSaleCache(server);
                SaveCacheTimes(server);
                StatusText.Text = $"Loaded FFXIAH prices for {toFetch.Count} items.";
            }
            catch (OperationCanceledException)
            {
                StatusText.Text = "FFXIAH price fetch canceled.";
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "Failed to fetch prices.\n" + ex.Message, "VanaCargo",
                    MessageBoxButton.OK, MessageBoxImage.Error);
                StatusText.Text = "FFXIAH price fetch failed.";
            }
            finally
            {
                FetchPricesButton.IsEnabled = true;
                StatusProgress.Visibility = Visibility.Collapsed;
                CancelFetchButton.Visibility = Visibility.Collapsed;
            }
        }

        private async Task<(string Median, string LastSale)> GetFfxiahValuesForItemAsync(int itemId, string itemName, string server, CancellationToken token)
        {
            _medianCache.TryGetValue(itemId, out var cachedMedian);
            _lastSaleCache.TryGetValue(itemId, out var cachedLastSale);

            if (!string.IsNullOrEmpty(cachedMedian) &&
                !string.IsNullOrEmpty(cachedLastSale) &&
                cachedLastSale != "0")
                return (cachedMedian, cachedLastSale);

            var url = BuildFfxiahUrl(itemId, itemName, server);
            using (var response = await _httpClient.GetAsync(url, token).ConfigureAwait(false))
            {
                response.EnsureSuccessStatusCode();
                var html = await response.Content.ReadAsStringAsync().ConfigureAwait(false);
                var median = cachedMedian ?? ExtractMedianForServer(html, server) ?? "0";
                if (median == "0")
                    median = "N/A";
                var lastSale = cachedLastSale ?? ExtractLastSale(html, server);
                if (string.IsNullOrEmpty(lastSale) || lastSale == "0")
                    lastSale = "N/A";
                _medianCache[itemId] = median;
                _lastSaleCache[itemId] = lastSale;
                _ffxiahCacheTimes[itemId] = DateTimeOffset.UtcNow;
                return (median, lastSale);
            }
        }

        private void UpdateFfxiahValues(int itemId, string median, string lastSale)
        {
            foreach (var tab in _currentTabs)
            {
                if (tab.Items == null)
                    continue;

                foreach (var item in tab.Items)
                {
                    if (item.Id == itemId)
                    {
                        if (!string.IsNullOrEmpty(median))
                            item.Median = median;
                        if (!string.IsNullOrEmpty(lastSale))
                            item.LastSale = lastSale;
                    }
                }
            }
        }

        private void RefreshGrids()
        {
            foreach (var tabItemObj in InventoryTabs.Items)
            {
                if (tabItemObj is TabItem tabItem && tabItem.Content is DataGrid grid)
                    grid.Items.Refresh();
            }
        }

        private void UpdateStatusProgress(int completed, int total)
        {
            var percent = total > 0 ? (completed * 100) / total : 0;
            StatusText.Text = $"Loading FFXIAH prices... {completed}/{total} ({percent}%)";
            StatusProgress.Value = completed;
            CancelFetchButton.Visibility = Visibility.Visible;
        }

        private void OnCancelFetch(object sender, RoutedEventArgs e)
        {
            _fetchCts?.Cancel();
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

            var includeMedian = selectedColumns.Any(c => c.Key == "Median");
            var includeLastSale = selectedColumns.Any(c => c.Key == "LastSale");
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

            var fetchMissing = exportDialog.FetchMissingPrices && (includeMedian || includeLastSale);
            if (fetchMissing && string.IsNullOrWhiteSpace(_currentServer))
            {
                MessageBox.Show(this, "Select a server before fetching FFXIAH prices.", "VanaCargo",
                    MessageBoxButton.OK, MessageBoxImage.Information);
                fetchMissing = false;
            }

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

                if ((includeMedian || includeLastSale) && fetchMissing)
                {
                    var missingIds = rows
                        .Where(r => r.Item != null &&
                            r.Item.Id > 0 &&
                            !string.Equals(r.Location, "Key Items", StringComparison.OrdinalIgnoreCase))
                        .Select(r => r.Item.Id)
                        .Distinct()
                        .Where(id =>
                            (includeMedian && (!_medianCache.ContainsKey(id) || !IsCacheFresh(id))) ||
                            (includeLastSale && (!_lastSaleCache.ContainsKey(id) || !IsCacheFresh(id))))
                        .ToList();

                    for (var i = 0; i < missingIds.Count; i++)
                    {
                        if (progress.IsCanceled)
                        {
                            StatusText.Text = "Export canceled.";
                            return;
                        }

                        var id = missingIds[i];
                        var values = await GetFfxiahValuesForItemAsync(id, null, _currentServer, CancellationToken.None);
                        var median = values.Median ?? "0";
                        if (median == "0")
                            median = "N/A";
                        _medianCache[id] = median;
                        _lastSaleCache[id] = string.IsNullOrEmpty(values.LastSale) ? "N/A" : values.LastSale;
                        _ffxiahCacheTimes[id] = DateTimeOffset.UtcNow;
                        progress.UpdateStatus($"Fetching FFXIAH prices... {i + 1}/{missingIds.Count}", i + 1, missingIds.Count);
                    }

                    SaveMedianCache(_currentServer);
                    SaveLastSaleCache(_currentServer);
                    SaveCacheTimes(_currentServer);
                }

                var totalRows = rows.Count;
                progress.UpdateStatus("Writing export file...", 0, totalRows);

                await Task.Run(() =>
                {
                    using (var writer = new StreamWriter(saveDialog.FileName, false, new System.Text.UTF8Encoding(true)))
                    {
                        var header = new System.Collections.Generic.List<string> { "Character", "Location" };
                        if (includeName) header.Add("Name");
                        if (includeAttr) header.Add("Attributes");
                        if (includeDesc) header.Add("Description");
                        if (includeType) header.Add("Type");
                        if (includeRaces) header.Add("Races");
                        if (includeLevel) header.Add("Level");
                        if (includeJobs) header.Add("Jobs");
                        if (includeRemarks) header.Add("Remarks");
                        if (includeBg) header.Add("BG Wiki URL");
                        if (includeMedian) header.Add("Avg FFXIAH Price");
                        if (includeLastSale) header.Add("Last FFXIAH Sale");
                        if (includeCount) header.Add("Count");
                        
                        WriteCsvRow(writer, header);

                        for (var i = 0; i < totalRows; i++)
                        {
                            if (progress.IsCanceled)
                                return;

                            var row = rows[i];
                            var cols = new System.Collections.Generic.List<string>
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
                            if (includeMedian)
                            {
                                if (!_medianCache.TryGetValue(row.Item.Id, out var median))
                                    median = string.IsNullOrEmpty(row.Item.Median) ? "N/A" : row.Item.Median;
                                cols.Add(median);
                            }
                            if (includeLastSale)
                            {
                                if (!_lastSaleCache.TryGetValue(row.Item.Id, out var lastSale))
                                    lastSale = string.IsNullOrEmpty(row.Item.LastSale) ? "N/A" : row.Item.LastSale;
                                cols.Add(lastSale);
                            }
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
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(saveDialog.FileName) { UseShellExecute = true });
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

        private System.Collections.Generic.List<ExportRow> LoadExportRows(IReadOnlyList<ExportDialog.CharacterOption> selectedChars, ExportProgressWindow progress)
        {
            var rows = new System.Collections.Generic.List<ExportRow>();
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

        private static void WriteCsvRow(StreamWriter writer, System.Collections.Generic.IReadOnlyList<string> values)
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

        private void LoadMedianCache(string server)
        {
            _medianCache.Clear();
            var entries = _bridge.LoadFfxiahCache(_configPath, server);
            if (entries == null)
                return;

            foreach (var entry in entries)
            {
                if (entry == null || entry.ItemId <= 0)
                    continue;

                var median = entry.Median ?? "0";
                if (median == "0")
                    median = "N/A";
                _medianCache[entry.ItemId] = median;
            }
        }

        private void LoadLastSaleCache(string server)
        {
            _lastSaleCache.Clear();
            var entries = _bridge.LoadFfxiahLastSaleCache(_configPath, server);
            if (entries == null)
                return;

            foreach (var entry in entries)
            {
                if (entry == null || entry.ItemId <= 0)
                    continue;

                var lastSale = entry.LastSale;
                if (string.IsNullOrEmpty(lastSale) || lastSale == "0")
                    lastSale = "N/A";
                _lastSaleCache[entry.ItemId] = lastSale;
            }
        }

        private void LoadCacheTimes(string server)
        {
            _ffxiahCacheTimes.Clear();
            var entries = _bridge.LoadFfxiahCacheTimes(_configPath, server);
            if (entries == null)
                return;

            foreach (var entry in entries)
            {
                if (entry == null || entry.ItemId <= 0)
                    continue;

                var timestamp = entry.Timestamp;
                if (timestamp <= 0)
                    continue;

                _ffxiahCacheTimes[entry.ItemId] = DateTimeOffset.FromUnixTimeSeconds(timestamp);
            }
        }

        private void SaveMedianCache(string server)
        {
            var entries = _medianCache
                .Select(pair => new ManagedMedianEntry { ItemId = pair.Key, Median = pair.Value })
                .ToArray();
            _bridge.SaveFfxiahCache(_configPath, server, entries);
        }

        private void SaveCacheTimes(string server)
        {
            var entries = _ffxiahCacheTimes
                .Select(pair => new ManagedCacheTimeEntry
                {
                    ItemId = pair.Key,
                    Timestamp = pair.Value.ToUnixTimeSeconds()
                })
                .ToArray();
            _bridge.SaveFfxiahCacheTimes(_configPath, server, entries);
        }

        private void SaveLastSaleCache(string server)
        {
            var entries = _lastSaleCache
                .Select(pair => new ManagedLastSaleEntry { ItemId = pair.Key, LastSale = pair.Value })
                .ToArray();
            _bridge.SaveFfxiahLastSaleCache(_configPath, server, entries);
        }

        private void ApplyCachedFfxiahValues()
        {
            if (_currentTabs == null)
                return;

            foreach (var tab in _currentTabs)
            {
                if (tab.Items == null)
                    continue;

                foreach (var item in tab.Items)
                {
                    if (_medianCache.TryGetValue(item.Id, out var cached))
                        item.Median = cached;
                    else
                        item.Median = "N/A";

                    if (_lastSaleCache.TryGetValue(item.Id, out var cachedLastSale))
                        item.LastSale = cachedLastSale;
                    else
                        item.LastSale = "N/A";
                }
            }
        }

        private bool IsCacheFresh(int itemId)
        {
            if (_loadResult?.Settings == null || !_loadResult.Settings.FfxiahCacheTtlEnabled)
                return true;

            if (!_ffxiahCacheTimes.TryGetValue(itemId, out var timestamp))
                return false;

            return DateTimeOffset.UtcNow - timestamp <= CacheTtl;
        }

        private static string ExtractMedianForServer(string html, string serverName)
        {
            var listStart = html.IndexOf("Item.server_medians", StringComparison.Ordinal);
            if (listStart < 0)
                return null;

            listStart = html.IndexOf('[', listStart);
            if (listStart < 0)
                return null;

            var listEnd = html.IndexOf("];", listStart, StringComparison.Ordinal);
            if (listEnd < 0)
                return null;

            var listData = html.Substring(listStart + 1, listEnd - listStart - 1);
            var objStart = 0;
            while ((objStart = listData.IndexOf('{', objStart)) >= 0)
            {
                var objEnd = listData.IndexOf('}', objStart);
                if (objEnd < 0)
                    break;

                var obj = listData.Substring(objStart, objEnd - objStart + 1);
                const string nameKey = "\"server_name\":\"";
                var namePos = obj.IndexOf(nameKey, StringComparison.Ordinal);
                if (namePos >= 0)
                {
                    namePos += nameKey.Length;
                    var nameEnd = obj.IndexOf('\"', namePos);
                    if (nameEnd > namePos)
                    {
                        var name = obj.Substring(namePos, nameEnd - namePos);
                        if (string.Equals(name, serverName, StringComparison.OrdinalIgnoreCase))
                        {
                            const string medianKey = "\"median\":";
                            var medianPos = obj.IndexOf(medianKey, StringComparison.Ordinal);
                            if (medianPos >= 0)
                            {
                                medianPos += medianKey.Length;
                                var digits = new string(obj.Skip(medianPos).TakeWhile(char.IsDigit).ToArray());
                                if (!string.IsNullOrEmpty(digits) && ulong.TryParse(digits, out var value))
                                    return value.ToString("N0", CultureInfo.InvariantCulture);
                            }
                        }
                    }
                }

                objStart = objEnd + 1;
            }

            return null;
        }

        private static string ExtractLastSale(string html, string serverName)
        {
            if (string.IsNullOrEmpty(html))
                return null;

            var marker = "id=\"sales-last\"";
            var index = html.IndexOf(marker, StringComparison.OrdinalIgnoreCase);
            if (index < 0)
            {
                marker = "id='sales-last'";
                index = html.IndexOf(marker, StringComparison.OrdinalIgnoreCase);
            }
            if (index < 0)
                return ExtractLastSaleFromSalesArray(html, serverName);

            var start = html.IndexOf('>', index);
            if (start < 0)
                return ExtractLastSaleFromSalesArray(html, serverName);

            start += 1;
            var end = html.IndexOf('<', start);
            if (end <= start)
                return ExtractLastSaleFromSalesArray(html, serverName);

            var value = html.Substring(start, end - start);
            var normalized = NormalizeFfxiahNumber(value);
            if (normalized == "0")
                return ExtractLastSaleFromSalesArray(html, serverName);

            return normalized;
        }

        private static string ExtractLastSaleFromSalesArray(string html, string serverName)
        {
            var listStart = html.IndexOf("Item.sales", StringComparison.Ordinal);
            if (listStart < 0)
                return null;

            listStart = html.IndexOf('[', listStart);
            if (listStart < 0)
                return null;

            var listEnd = html.IndexOf("];", listStart, StringComparison.Ordinal);
            if (listEnd < 0)
                return null;

            var listData = html.Substring(listStart + 1, listEnd - listStart - 1);
            var serverId = TryGetServerId(html, serverName);
            var bestSaleOn = -1L;
            var bestPrice = (string)null;

            var objStart = 0;
            while ((objStart = listData.IndexOf('{', objStart)) >= 0)
            {
                var objEnd = listData.IndexOf('}', objStart);
                if (objEnd < 0)
                    break;

                var obj = listData.Substring(objStart, objEnd - objStart + 1);
                var price = ParseIntField(obj, "\"price\":");
                var saleOn = ParseIntField(obj, "\"saleon\":");
                var sellerServer = ParseIntField(obj, "\"seller_server\":");
                var buyerServer = ParseIntField(obj, "\"buyer_server\":");

                var matchesServer = serverId > 0
                    ? (sellerServer == serverId || buyerServer == serverId)
                    : true;

                if (matchesServer && price >= 0)
                {
                    if (saleOn >= 0 && saleOn >= bestSaleOn)
                    {
                        bestSaleOn = saleOn;
                        bestPrice = price.ToString("N0", CultureInfo.InvariantCulture);
                    }
                    else if (bestPrice == null)
                    {
                        bestPrice = price.ToString("N0", CultureInfo.InvariantCulture);
                    }
                }

                objStart = objEnd + 1;
            }

            return bestPrice;
        }

        private static int TryGetServerId(string html, string serverName)
        {
            if (string.IsNullOrEmpty(html) || string.IsNullOrEmpty(serverName))
                return 0;

            var sid = ParseSiteSid(html, serverName);
            if (sid > 0)
                return sid;

            var optionStart = 0;
            const string optionMarker = "<option value='";
            while ((optionStart = html.IndexOf(optionMarker, optionStart, StringComparison.OrdinalIgnoreCase)) >= 0)
            {
                var valueStart = optionStart + optionMarker.Length;
                var valueEnd = html.IndexOf('\'', valueStart);
                if (valueEnd < 0)
                    break;

                var nameStart = html.IndexOf('>', valueEnd);
                if (nameStart < 0)
                    break;

                nameStart += 1;
                var nameEnd = html.IndexOf('<', nameStart);
                if (nameEnd < 0)
                    break;

                var name = html.Substring(nameStart, nameEnd - nameStart).Trim();
                if (string.Equals(name, serverName, StringComparison.OrdinalIgnoreCase))
                {
                    var valueText = html.Substring(valueStart, valueEnd - valueStart);
                    if (int.TryParse(valueText, out var parsed))
                        return parsed;
                }

                optionStart = nameEnd + 1;
            }

            return 0;
        }

        private static int ParseSiteSid(string html, string serverName)
        {
            var serverMarker = "Site.server = \"";
            var serverIndex = html.IndexOf(serverMarker, StringComparison.OrdinalIgnoreCase);
            if (serverIndex >= 0)
            {
                serverIndex += serverMarker.Length;
                var serverEnd = html.IndexOf('"', serverIndex);
                if (serverEnd > serverIndex)
                {
                    var currentServer = html.Substring(serverIndex, serverEnd - serverIndex);
                    if (!string.Equals(currentServer, serverName, StringComparison.OrdinalIgnoreCase))
                        return 0;
                }
            }

            var sidMarker = "Site.sid = \"";
            var sidIndex = html.IndexOf(sidMarker, StringComparison.OrdinalIgnoreCase);
            if (sidIndex < 0)
                return 0;

            sidIndex += sidMarker.Length;
            var sidEnd = html.IndexOf('"', sidIndex);
            if (sidEnd <= sidIndex)
                return 0;

            var sidText = html.Substring(sidIndex, sidEnd - sidIndex);
            return int.TryParse(sidText, out var parsed) ? parsed : 0;
        }

        private static long ParseIntField(string obj, string key)
        {
            var pos = obj.IndexOf(key, StringComparison.Ordinal);
            if (pos < 0)
                return -1;

            pos += key.Length;
            var digits = new string(obj.Skip(pos).TakeWhile(char.IsDigit).ToArray());
            if (string.IsNullOrEmpty(digits))
                return -1;

            return long.TryParse(digits, out var value) ? value : -1;
        }

        private static string BuildFfxiahUrl(int itemId, string itemName, string serverName)
        {
            var slug = BuildItemSlug(itemName);
            var url = string.IsNullOrEmpty(slug)
                ? $"https://www.ffxiah.com/item/{itemId}/"
                : $"https://www.ffxiah.com/item/{itemId}/{slug}";

            if (!string.IsNullOrEmpty(serverName) &&
                FfxiahServerIds.TryGetValue(serverName, out var serverId) &&
                serverId > 0)
            {
                url += $"?sid={serverId}";
            }

            return url;
        }

        private static string BuildItemSlug(string name)
        {
            if (string.IsNullOrWhiteSpace(name))
                return string.Empty;

            var builder = new System.Text.StringBuilder();
            var lastDash = false;

            foreach (var ch in name.ToLowerInvariant())
            {
                if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
                {
                    builder.Append(ch);
                    lastDash = false;
                }
                else if (ch == ' ' || ch == '-' || ch == '_')
                {
                    if (!lastDash && builder.Length > 0)
                    {
                        builder.Append('-');
                        lastDash = true;
                    }
                }
            }

            var slug = builder.ToString().Trim('-');
            return slug;
        }

        private static string NormalizeFfxiahNumber(string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return "0";

            var digits = new string(value.Where(char.IsDigit).ToArray());
            if (string.IsNullOrEmpty(digits))
                return "0";

            if (ulong.TryParse(digits, out var parsed))
                return parsed.ToString("N0", CultureInfo.InvariantCulture);

            return "0";
        }

        private static Style CreateNoWrapCellStyle()
        {
            var style = new Style(typeof(TextBlock));
            style.Setters.Add(new Setter(TextBlock.TextWrappingProperty, TextWrapping.NoWrap));
            style.Setters.Add(new Setter(TextBlock.TextTrimmingProperty, TextTrimming.CharacterEllipsis));
            style.Setters.Add(new Setter(TextBlock.VerticalAlignmentProperty, VerticalAlignment.Center));
            return style;
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
            grid.ContextMenu = CreateGridContextMenu(grid, isKeyItems);

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
                grid.Columns.Add(new DataGridTextColumn { Header = "Avg FFXIAH Price", Binding = new Binding("Median"), ElementStyle = noWrapStyle });
                grid.Columns.Add(new DataGridTextColumn { Header = "Last FFXIAH Sale", Binding = new Binding("LastSale"), ElementStyle = noWrapStyle });
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
                var uri = new Uri("pack://application:,,,/VanaCargoApp;component/Assets/safe16alpha.ico", UriKind.Absolute);
                var bitmap = new BitmapImage();
                bitmap.BeginInit();
                bitmap.UriSource = uri;
                bitmap.CacheOption = BitmapCacheOption.OnLoad;
                bitmap.EndInit();
                bitmap.Freeze();
                return bitmap;
            }
            catch
            {
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

            var window = new SearchWindow(_bridge, _loadResult, _currentServer)
            {
                Owner = this
            };
            window.Show();
        }

        private void OnOpenConfig(object sender, RoutedEventArgs e)
        {
            if (_loadResult?.Settings == null)
                return;

            var window = new SettingsWindow(_loadResult.Settings, _loadResult.Characters, FfxiahServers, _currentServer)
            {
                Owner = this
            };

            if (window.ShowDialog() != true)
                return;

            var selectedId = (CharactersList.SelectedItem as ManagedCharacter)?.Id;
            var prevFindAll = _loadResult.Settings.FindAllEnabled;

            _loadResult.Settings.FfxiahCacheTtlEnabled = window.FfxiahCacheTtlEnabled;
            _loadResult.Settings.FindAllEnabled = window.FindAllEnabled;
            _loadResult.Settings.FindAllDataPath = window.FindAllDataPath;
            _loadResult.Settings.FindAllKeyItemsPath = window.FindAllKeyItemsPath;
            _loadResult.Settings.FfxiahServer = window.SelectedServer ?? string.Empty;
            _bridge.SaveSettings(_configPath, _loadResult.Settings);
            SaveCharacterDisplayNames(window.Characters);
            if (!string.Equals(_currentServer, _loadResult.Settings.FfxiahServer, StringComparison.OrdinalIgnoreCase))
            {
                _currentServer = _loadResult.Settings.FfxiahServer ?? string.Empty;
                LoadMedianCache(_currentServer);
                LoadLastSaleCache(_currentServer);
                LoadCacheTimes(_currentServer);
                ApplyCachedFfxiahValues();
                RefreshGrids();
            }
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
            _currentServer = _loadResult.Settings.FfxiahServer ?? string.Empty;
            LoadMedianCache(_currentServer);
            LoadLastSaleCache(_currentServer);
            LoadCacheTimes(_currentServer);

            if (!string.IsNullOrEmpty(selectedId))
            {
                var match = _loadResult.Characters.FirstOrDefault(c => c.Id == selectedId);
                if (match != null)
                    CharactersList.SelectedItem = match;
            }
        }

        public ManagedCharacter GetSelectedCharacter()
        {
            return CharactersList.SelectedItem as ManagedCharacter;
        }

        public void EnsureFindAllPathsPublic()
        {
            EnsureFindAllPaths();
        }

        private void PromptForServer()
        {
            var dialog = new ServerSelectWindow(FfxiahServers, _currentServer)
            {
                Owner = this
            };

            if (dialog.ShowDialog() == true)
            {
                _currentServer = dialog.SelectedServer;
                _loadResult.Settings.FfxiahServer = _currentServer;
                _bridge.SaveSettings(_configPath, _loadResult.Settings);
                LoadMedianCache(_currentServer);
                LoadLastSaleCache(_currentServer);
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

        private ContextMenu CreateGridContextMenu(DataGrid grid, bool isKeyItems)
        {
            var menu = new ContextMenu();
            var openBg = new MenuItem { Header = "Open BG Wiki", Tag = grid };
            openBg.Click += OnOpenBgWiki;
            menu.Items.Add(openBg);
            if (!isKeyItems)
            {
                var openFfxiah = new MenuItem { Header = "Open FFXIAH", Tag = grid };
                openFfxiah.Click += OnOpenFfxiah;
                menu.Items.Add(openFfxiah);
            }
            return menu;
        }

        private static DataGridCell FindParentCell(DependencyObject source)
        {
            var current = source;
            while (current != null && !(current is DataGridCell))
                current = System.Windows.Media.VisualTreeHelper.GetParent(current);

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
