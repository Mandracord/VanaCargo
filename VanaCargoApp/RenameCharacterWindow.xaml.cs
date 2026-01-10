using System.Windows;

namespace VanaCargoApp
{
    public partial class RenameCharacterWindow : Window
    {
        public string DisplayName { get; private set; }

        public RenameCharacterWindow(string currentName)
        {
            InitializeComponent();
            NameBox.Text = currentName ?? string.Empty;
            NameBox.SelectAll();
            NameBox.Focus();
        }

        private void OnOk(object sender, RoutedEventArgs e)
        {
            DisplayName = NameBox.Text?.Trim();
            DialogResult = true;
        }
    }
}
