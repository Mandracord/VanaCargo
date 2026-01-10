using System.Windows;

namespace VanaCargoApp
{
    public partial class ExportProgressWindow : Window
    {
        public bool IsCanceled { get; private set; }

        public ExportProgressWindow()
        {
            InitializeComponent();
        }

        public void UpdateStatus(string text, int current, int total)
        {
            StatusText.Text = text;
            ProgressBar.Maximum = total > 0 ? total : 1;
            ProgressBar.Value = current;
        }

        private void OnCancel(object sender, RoutedEventArgs e)
        {
            IsCanceled = true;
            CancelButton.IsEnabled = false;
        }
    }
}
