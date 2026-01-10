using System;
using System.Collections.ObjectModel;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using VanaCargoBridge;

namespace VanaCargoApp
{
    public partial class SearchWindow : Window
    {
        private readonly CoreBridge _bridge;
        private readonly LoadResult _loadResult;
        private readonly CancellationTokenSource _cts = new CancellationTokenSource();
        private readonly ObservableCollection<SearchResultRow> _results = new ObservableCollection<SearchResultRow>();

        public IconConverter IconConverter { get; } = new IconConverter();

        public SearchWindow(CoreBridge bridge, LoadResult loadResult)
        {
            _bridge = bridge;
            _loadResult = loadResult;
            InitializeComponent();
            ResultsGrid.ItemsSource = _results;
        }

        private async void OnSearchClick(object sender, RoutedEventArgs e)
        {
            var term = SearchBox.Text?.Trim() ?? string.Empty;
            if (string.IsNullOrEmpty(term))
                return;

            var allChars = AllCharactersCheck.IsChecked == true;
            var characters = allChars ? _loadResult.Characters : GetSelectedCharacter();
            if (characters == null || characters.Length == 0)
            {
                StatusText.Text = "Select a character first.";
                return;
            }

            SearchButton.IsEnabled = false;
            CancelButton.IsEnabled = true;
            StatusText.Text = "Searching...";
            _results.Clear();

            try
            {
                await Task.Run(() => ExecuteSearch(term, characters), _cts.Token);
                StatusText.Text = $"Found {_results.Count} results.";
            }
            catch (OperationCanceledException)
            {
                StatusText.Text = "Search canceled.";
            }
            finally
            {
                SearchButton.IsEnabled = true;
                CancelButton.IsEnabled = false;
            }
        }

        private void ExecuteSearch(string term, ManagedCharacter[] characters)
        {
            foreach (var character in characters)
            {
                _cts.Token.ThrowIfCancellationRequested();
                var tabs = _bridge.LoadInventoryForCharacter(_loadResult.Settings, character, _loadResult.Tabs);
                if (tabs == null)
                    continue;

                foreach (var tab in tabs)
                {
                    if (tab.Items == null)
                        continue;

                    foreach (var item in tab.Items)
                    {
                        if (!Matches(item, term))
                            continue;

                        Dispatcher.Invoke(() =>
                        {
                            _results.Add(new SearchResultRow
                            {
                                Character = character.Name,
                                Location = tab.Info.DisplayName,
                                Item = item
                            });
                        });
                    }
                }
            }
        }

        private ManagedCharacter[] GetSelectedCharacter()
        {
            if (Owner is MainWindow owner && owner.CharactersList.SelectedItem is ManagedCharacter selected)
                return new[] { selected };

            return Array.Empty<ManagedCharacter>();
        }

        private void OnCancelClick(object sender, RoutedEventArgs e)
        {
            _cts.Cancel();
        }

        private void OnResultDoubleClick(object sender, System.Windows.Input.MouseButtonEventArgs e)
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
            if (!string.Equals(bindingPath, "Item.Name", StringComparison.OrdinalIgnoreCase) &&
                !string.Equals(header, "Name", StringComparison.OrdinalIgnoreCase))
                return;

            var row = grid.SelectedItem as SearchResultRow;
            if (row?.Item == null)
                return;

            var url = BuildBgWikiUrl(row.Item.Name);
            try
            {
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(url) { UseShellExecute = true });
            }
            catch
            {
            }
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

        private static bool Matches(ManagedItem item, string term)
        {
            if (item == null)
                return false;

            return ContainsTerm(item.Name, term) ||
                   ContainsTerm(item.Attr, term) ||
                   ContainsTerm(item.Description, term) ||
                   ContainsTerm(item.Slot, term) ||
                   ContainsTerm(item.Races, term) ||
                   ContainsTerm(item.Level, term) ||
                   ContainsTerm(item.Jobs, term) ||
                   ContainsTerm(item.Remarks, term) ||
                   item.Id.ToString(CultureInfo.InvariantCulture).IndexOf(term, StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private static bool ContainsTerm(string value, string term)
        {
            if (string.IsNullOrEmpty(value))
                return false;

            return value.IndexOf(term, StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private sealed class SearchResultRow
        {
            public string Character { get; set; }
            public string Location { get; set; }
            public ManagedItem Item { get; set; }
        }
    }
}
