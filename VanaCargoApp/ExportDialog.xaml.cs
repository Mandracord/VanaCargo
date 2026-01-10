using System.Collections.Generic;
using System.Linq;
using System.Windows;
using VanaCargoBridge;

namespace VanaCargoApp
{
    public partial class ExportDialog : Window
    {
        public sealed class CharacterOption
        {
            public string Id { get; set; }
            public string Name { get; set; }
            public bool IsSelected { get; set; }
        }

        public sealed class ColumnOption
        {
            public string Key { get; set; }
            public string Label { get; set; }
            public bool IsSelected { get; set; }
        }

        private readonly List<CharacterOption> _characters = new List<CharacterOption>();
        private readonly List<ColumnOption> _columns = new List<ColumnOption>();

        public ExportDialog(IEnumerable<ManagedCharacter> characters)
        {
            InitializeComponent();

            foreach (var character in characters)
            {
                _characters.Add(new CharacterOption
                {
                    Id = character.Id,
                    Name = character.Name,
                    IsSelected = true
                });
            }

            _columns.Add(new ColumnOption { Key = "Name", Label = "Name", IsSelected = true });
            _columns.Add(new ColumnOption { Key = "Count", Label = "Count", IsSelected = true });
            _columns.Add(new ColumnOption { Key = "Attr", Label = "Attributes", IsSelected = true });
            _columns.Add(new ColumnOption { Key = "Description", Label = "Description", IsSelected = true });
            _columns.Add(new ColumnOption { Key = "Type", Label = "Type", IsSelected = true });
            _columns.Add(new ColumnOption { Key = "Races", Label = "Races", IsSelected = true });
            _columns.Add(new ColumnOption { Key = "Level", Label = "Level", IsSelected = true });
            _columns.Add(new ColumnOption { Key = "Jobs", Label = "Jobs", IsSelected = true });
            _columns.Add(new ColumnOption { Key = "Remarks", Label = "Remarks", IsSelected = true });
            _columns.Add(new ColumnOption { Key = "BgWiki", Label = "BG Wiki URL", IsSelected = true });

            CharactersList.ItemsSource = _characters;
            ColumnsList.ItemsSource = _columns;
        }

        public IReadOnlyList<CharacterOption> SelectedCharacters => _characters.Where(c => c.IsSelected).ToList();
        public IReadOnlyList<ColumnOption> SelectedColumns => _columns.Where(c => c.IsSelected).ToList();

        private void OnOk(object sender, RoutedEventArgs e)
        {
            DialogResult = true;
        }
    }
}
