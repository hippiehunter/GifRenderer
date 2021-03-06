﻿using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Foundation;
using Windows.Foundation.Collections;
using Windows.Storage;
using Windows.Storage.Search;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Controls.Primitives;
using Windows.UI.Xaml.Data;
using Windows.UI.Xaml.Input;
using Windows.UI.Xaml.Media;
using Windows.UI.Xaml.Navigation;

// The Blank Page item template is documented at http://go.microsoft.com/fwlink/?LinkId=402352&clcid=0x409

namespace GifRenderer.Test.Universal
{
    /// <summary>
    /// An empty page that can be used on its own or navigated to within a Frame.
    /// </summary>
    public sealed partial class MainPage : Page
    {
        public MainPage()
        {
            this.InitializeComponent();
        }

        private async void Page_Loaded(object sender, RoutedEventArgs e)
        {
            //var testFolder = await KnownFolders.PicturesLibrary.GetFolderAsync("test");
            //var files = await testFolder.GetFilesAsync();
            picturesSource.Source = new string[]
                {
                    "http://media.riffsy.com/images/abb83619f7cc8401e270eadb90937118/raw",
                    "http://33.media.tumblr.com/67d4f233ae1080413313cd33af61888d/tumblr_mx9vdcr34f1t4taudo1_400.gif",
                    "http://i.imgur.com/TtCvL2i.gif"

                };//files.Select(file => file.Path);
        }
    }
}
