using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Threading.Tasks;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.Storage.Streams;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;
using Windows.Web.Http;

// The Blank Page item template is documented at http://go.microsoft.com/fwlink/?LinkId=234238

namespace TestUI
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainPage : Page, INotifyPropertyChanged
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
			DataContext = this;
        }

		public object data {get; set;}
		public List<string> TestUrls = new List<string>
		{
			"https://imagetestsuite.googlecode.com/svn/trunk/gif/0646caeb9b9161c777f117007921a687.gif"
		};
		private async void Button_Click(object sender, RoutedEventArgs e)
		{
			HttpClient client = new HttpClient();
			var response = await client.GetAsync(new Uri(TestUrls.LastOrDefault()), HttpCompletionOption.ResponseHeadersRead);
			var responseStream = await response.Content.ReadAsInputStreamAsync();
			var initialBuffer = await responseStream.ReadAsync(new Windows.Storage.Streams.Buffer(4096), 4096, InputStreamOptions.None);
			if (initialBuffer.Length == 0)
				throw new Exception("failed to read initial bytes of image");

			var bufferBytes = new byte[initialBuffer.Length];
			initialBuffer.CopyTo(bufferBytes);

			data = new GifRenderer.GifPayload { initialData = bufferBytes.ToList(), inputStream = responseStream, url = TestUrls.LastOrDefault()};

			TestUrls.Remove(TestUrls.Last());

			if (PropertyChanged != null)
				PropertyChanged(this, new PropertyChangedEventArgs("data"));
			data = null;
		}

		public event PropertyChangedEventHandler PropertyChanged;
	}
}
