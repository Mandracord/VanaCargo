using System;
using System.Diagnostics;
using System.Windows;

namespace VanaCargoApp
{
    public partial class AboutWindow : Window
    {
        public string SourceUrl { get; }
        public string CopyrightOriginal { get; }
        public string CopyrightCurrent { get; }

        public AboutWindow()
        {
            SourceUrl = "https://github.com/TeoTwawki/LootBox";
            CopyrightOriginal = $"Copyright (c) 2015 - 2025 Teo Twawki";
            CopyrightCurrent = $"Copyright (c) 2025 - 2026 Mandracord";

            DataContext = this;
            InitializeComponent();
        }

        private void OnClose(object sender, RoutedEventArgs e)
        {
            Close();
        }

        private void OnSourceClick(object sender, RoutedEventArgs e)
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = SourceUrl,
                UseShellExecute = true
            });
        }
    }
}
