using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Http;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading.Tasks;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;

// The Blank Page item template is documented at http://go.microsoft.com/fwlink/?LinkId=234238

namespace TestUI
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        public MainPage()
        {
            this.InitializeComponent();

            this.NavigationCacheMode = NavigationCacheMode.Required;
        }

        /// <summary>
        /// Invoked when this page is about to be displayed in a Frame.
        /// </summary>
        /// <param name="e">Event data that describes how this page was reached.
        /// This parameter is typically used to configure the page.</param>
        protected override void OnNavigatedTo(NavigationEventArgs e)
        {
            // TODO: Prepare page for display here.

            // TODO: If your application contains multiple pages, ensure that you are
            // handling the hardware Back button by registering for the
            // Windows.Phone.UI.Input.HardwareButtons.BackPressed event.
            // If you are using the NavigationHelper provided by some templates,
            // this event is handled for you.
        }

		private async void Button_Click(object sender, RoutedEventArgs e)
		{
			HttpClient client = new HttpClient();
			var response = await client.GetAsync(theUrl.Text, HttpCompletionOption.ResponseHeadersRead);
			var responseStream = await response.Content.ReadAsStreamAsync();
			var memoryStream = new MemoryStream();
			var initialBuffer = new byte[4096];
			var initialReadLength = await responseStream.ReadAsync(initialBuffer, 0, 4096);
			if (initialReadLength == 0)
				throw new Exception("failed to read initial bytes of image");
			memoryStream.Write(initialBuffer, 0, initialReadLength);
			long returnedPosition = 0;
			bool finished = false;
			GifRenderer.GetMoreData getter = () =>
			{
				//its over
				if (finished && returnedPosition == memoryStream.Length)
					return null;

				if (returnedPosition < memoryStream.Length)
				{
					lock(memoryStream)
					{
						var result = new byte[memoryStream.Length - returnedPosition];
						memoryStream.Seek(returnedPosition, SeekOrigin.Begin);
						memoryStream.Read(result, 0, result.Length);
						returnedPosition += result.Length;
						return result;
					}
				}
				else
					return new byte[0];
			};
			Task.Run(async () =>
				{
					try
					{
						var buffer = new byte[4096];
						for(;;)
						{
							var readBytes = await responseStream.ReadAsync(buffer, 0, 4096);
							if (readBytes == 0)
								break;
							else
							{
								lock (memoryStream)
								{
									memoryStream.Seek(0, SeekOrigin.End);
									memoryStream.Write(buffer, 0, readBytes);
								}
							}
						}
					}
					catch { }
					finally
					{
						finished = true;
					}
				});
			renderer = new GifRenderer.GifRenderer(getter);
			theImage.Source = renderer.ImageSource;
			Button2_Click(sender, e);
		}

		GifRenderer.GifRenderer renderer;
		GifRenderer.GifRenderer renderer2;

		private async void Button2_Click(object sender, RoutedEventArgs e)
		{
			HttpClient client = new HttpClient();
			var response = await client.GetAsync(theUrl.Text, HttpCompletionOption.ResponseHeadersRead);
			var responseStream = await response.Content.ReadAsStreamAsync();
			var memoryStream = new MemoryStream();
			var initialBuffer = new byte[4096];
			var initialReadLength = await responseStream.ReadAsync(initialBuffer, 0, 4096);
			if (initialReadLength == 0)
				throw new Exception("failed to read initial bytes of image");
			memoryStream.Write(initialBuffer, 0, initialReadLength);
			long returnedPosition = 0;
			bool finished = false;
			GifRenderer.GetMoreData getter = () =>
			{
				//its over
				if (finished && returnedPosition == memoryStream.Length)
					return null;

				if (returnedPosition < memoryStream.Length)
				{
					lock (memoryStream)
					{
						var result = new byte[memoryStream.Length - returnedPosition];
						memoryStream.Seek(returnedPosition, SeekOrigin.Begin);
						memoryStream.Read(result, 0, result.Length);
						returnedPosition += result.Length;
						return result;
					}
				}
				else
					return new byte[0];
			};
			Task.Run(async () =>
			{
				try
				{
					var buffer = new byte[4096];
					for (; ; )
					{
						var readBytes = await responseStream.ReadAsync(buffer, 0, 4096);
						if (readBytes == 0)
							break;
						else
						{
							lock (memoryStream)
							{
								memoryStream.Seek(0, SeekOrigin.End);
								memoryStream.Write(buffer, 0, readBytes);
							}
						}
					}
				}
				catch { }
				finally
				{
					finished = true;
				}
			});
			renderer2 = new GifRenderer.GifRenderer(getter);
			theImage2.Source = renderer2.ImageSource;
		}
    }
}
