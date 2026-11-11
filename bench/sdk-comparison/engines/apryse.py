import os
import sys

from apryse_sdk import (ImageSettings, MonoImageSettings, Optimizer, OptimizerSettings, PDFDoc,
                        PDFNet, SDFDoc, TextSettings)

LEVELS = {'low': (200, 8), 'medium': (150, 5), 'high': (72, 2)}

src, dst, level = sys.argv[1:4]
PDFNet.Initialize(os.environ['APRYSE_KEY'])
doc = PDFDoc(src)
doc.InitSecurityHandler()
color, mono, text = ImageSettings(), MonoImageSettings(), TextSettings()
text.SubsetFonts(True)
mono.SetDownsampleMode(MonoImageSettings.e_off)
dpi, quality = LEVELS[level]
color.SetCompressionMode(ImageSettings.e_jpeg)
color.SetQuality(quality)
color.SetImageDPI(dpi * 1.5, dpi)
color.ForceRecompression(True)
mono.SetCompressionMode(MonoImageSettings.e_jbig2)
mono.SetJBIG2Threshold(10)
settings = OptimizerSettings()
settings.SetColorImageSettings(color)
settings.SetGrayscaleImageSettings(color)
settings.SetMonoImageSettings(mono)
settings.SetTextSettings(text)
Optimizer.Optimize(doc, settings)
doc.Save(dst, SDFDoc.e_remove_unused)
doc.Close()
PDFNet.Terminate()
