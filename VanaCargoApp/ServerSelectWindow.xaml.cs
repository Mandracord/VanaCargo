using System.Collections.Generic;
using System.Windows;

namespace VanaCargoApp
{
    public partial class ServerSelectWindow : Window
    {
        public string SelectedServer => ServerCombo.Text?.Trim() ?? string.Empty;

        public ServerSelectWindow(IEnumerable<string> servers, string currentServer)
        {
            InitializeComponent();
            ServerCombo.ItemsSource = servers;
            ServerCombo.Text = currentServer ?? string.Empty;
        }

        private void OnOk(object sender, RoutedEventArgs e)
        {
            DialogResult = true;
        }
    }
}
