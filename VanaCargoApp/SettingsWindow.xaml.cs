using System.Collections.Generic;
using System.Linq;
using System.Windows;
using Microsoft.Win32;
using VanaCargoBridge;
using Forms = System.Windows.Forms;

namespace VanaCargoApp
{
    public partial class SettingsWindow : Window
    {
        public sealed class CharacterOption
        {
            public string Id { get; set; }
            public string Name { get; set; }

            public string DisplayLabel => string.IsNullOrEmpty(Name) ? Id : $"{Name} ({Id})";
        }

        private readonly List<CharacterOption> _characters = new List<CharacterOption>();

        public bool FfxiahCacheTtlEnabled { get; private set; }
        public bool FindAllEnabled { get; private set; }
        public string FindAllDataPath { get; private set; }
        public string FindAllKeyItemsPath { get; private set; }
        public string SelectedServer { get; private set; }
        public IReadOnlyList<CharacterOption> Characters => _characters;

        public SettingsWindow(ManagedSettings settings, IEnumerable<ManagedCharacter> characters, IEnumerable<string> servers, string currentServer)
        {
            InitializeComponent();

            if (servers != null)
                ServerCombo.ItemsSource = servers.ToList();
            SelectedServer = currentServer ?? string.Empty;
            ServerCombo.Text = SelectedServer;

            FfxiahCacheTtlEnabled = settings?.FfxiahCacheTtlEnabled ?? true;
            CacheTtlCheck.IsChecked = FfxiahCacheTtlEnabled;
            FindAllEnabled = settings?.FindAllEnabled ?? false;
            FindAllCheck.IsChecked = FindAllEnabled;
            FindAllDataPath = settings?.FindAllDataPath ?? string.Empty;
            FindAllKeyItemsPath = settings?.FindAllKeyItemsPath ?? string.Empty;
            FindAllDataPathBox.Text = FindAllDataPath;
            FindAllKeyItemsPathBox.Text = FindAllKeyItemsPath;

            if (characters != null)
            {
                foreach (var character in characters)
                {
                    _characters.Add(new CharacterOption
                    {
                        Id = character.Id,
                        Name = character.Name
                    });
                }
            }

            CharactersList.ItemsSource = _characters;
        }

        private void OnRename(object sender, RoutedEventArgs e)
        {
            var selected = CharactersList.SelectedItem as CharacterOption;
            if (selected == null)
                return;

            var dialog = new RenameCharacterWindow(selected.Name)
            {
                Owner = this
            };

            if (dialog.ShowDialog() != true)
                return;

            selected.Name = dialog.DisplayName;
            CharactersList.Items.Refresh();
        }

        private void OnOk(object sender, RoutedEventArgs e)
        {
            FfxiahCacheTtlEnabled = CacheTtlCheck.IsChecked == true;
            FindAllEnabled = FindAllCheck.IsChecked == true;
            FindAllDataPath = FindAllDataPathBox.Text?.Trim() ?? string.Empty;
            FindAllKeyItemsPath = FindAllKeyItemsPathBox.Text?.Trim() ?? string.Empty;
            SelectedServer = ServerCombo.Text?.Trim() ?? string.Empty;
            DialogResult = true;
        }

        private void OnBrowseDataPath(object sender, RoutedEventArgs e)
        {
            using (var dialog = new Forms.FolderBrowserDialog())
            {
                dialog.Description = "Select the FindAll data folder";
                dialog.SelectedPath = FindAllDataPathBox.Text;
                if (dialog.ShowDialog() == Forms.DialogResult.OK)
                    FindAllDataPathBox.Text = dialog.SelectedPath;
            }
        }

        private void OnBrowseKeyItems(object sender, RoutedEventArgs e)
        {
            var dialog = new OpenFileDialog
            {
                Title = "Select key_items.lua",
                Filter = "Lua files (*.lua)|*.lua|All files (*.*)|*.*",
                FileName = FindAllKeyItemsPathBox.Text
            };

            if (dialog.ShowDialog(this) == true)
                FindAllKeyItemsPathBox.Text = dialog.FileName;
        }
    }
}
