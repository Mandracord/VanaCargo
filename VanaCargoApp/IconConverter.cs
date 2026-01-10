using System;
using System.Globalization;
using System.IO;
using System.Windows.Data;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace VanaCargoApp
{
    public sealed class IconConverter : IMultiValueConverter
    {
        private const string DumpEnvVar = "VANACARGO_DUMP_ICON";
        private static bool _dumped;

        public object Convert(object[] values, Type targetType, object parameter, CultureInfo culture)
        {
            if (values == null || values.Length < 4)
                return null;

            var pixels = values[0] as byte[];
            if (pixels == null || pixels.Length == 0)
                return null;

            if (!(values[1] is int width) || !(values[2] is int height) || !(values[3] is int stride))
                return null;

            if (width <= 0 || height <= 0 || stride <= 0)
                return null;

            var bitmap = BitmapSource.Create(width, height, 96, 96, PixelFormats.Bgra32, null, pixels, stride);
            bitmap.Freeze();
            MaybeDump(bitmap);
            return bitmap;
        }

        public object[] ConvertBack(object value, Type[] targetTypes, object parameter, CultureInfo culture)
        {
            throw new NotSupportedException();
        }

        private static byte[] EnsurePremultiplied(byte[] pixels)
        {
            if (pixels == null || pixels.Length < 4)
                return pixels;

            bool needs = false;
            for (int i = 0; i + 3 < pixels.Length; i += 4)
            {
                byte a = pixels[i + 3];
                if (a == 0 || a == 255)
                    continue;

                if (pixels[i] > a || pixels[i + 1] > a || pixels[i + 2] > a)
                {
                    needs = true;
                    break;
                }
            }

            if (!needs)
                return pixels;

            var premultiplied = new byte[pixels.Length];
            for (int i = 0; i + 3 < pixels.Length; i += 4)
            {
                byte a = pixels[i + 3];
                premultiplied[i + 3] = a;
                if (a == 0)
                {
                    premultiplied[i] = 0;
                    premultiplied[i + 1] = 0;
                    premultiplied[i + 2] = 0;
                    continue;
                }

                premultiplied[i] = (byte)((pixels[i] * a + 127) / 255);
                premultiplied[i + 1] = (byte)((pixels[i + 1] * a + 127) / 255);
                premultiplied[i + 2] = (byte)((pixels[i + 2] * a + 127) / 255);
            }

            return premultiplied;
        }

        private static void MaybeDump(BitmapSource bitmap)
        {
            if (_dumped || bitmap == null)
                return;

            var flag = Environment.GetEnvironmentVariable(DumpEnvVar);
            if (!string.Equals(flag, "1", StringComparison.OrdinalIgnoreCase))
                return;

            var path = Path.Combine(Path.GetTempPath(), "vanacargo_icon_dump.png");
            if (File.Exists(path))
            {
                _dumped = true;
                return;
            }

            try
            {
                using (var stream = new FileStream(path, FileMode.Create, FileAccess.Write))
                {
                    var encoder = new PngBitmapEncoder();
                    encoder.Frames.Add(BitmapFrame.Create(bitmap));
                    encoder.Save(stream);
                }
                _dumped = true;
            }
            catch
            {
                _dumped = true;
            }
        }
    }
}
