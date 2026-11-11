using GdPicture14;

new LicenseManager().RegisterKEY(Environment.GetEnvironmentVariable("NUTRIENT_KEY") ?? "");

if (args[0] == "batch")
{
    var first = File.ReadLines(args[1]).First().Split('\t');
    var warm = Task.Run(() => Reduce(args[2], first[1] + ".warm.pdf", first[2]));
    warm.Wait(TimeSpan.FromSeconds(120));
    try { File.Delete(first[1] + ".warm.pdf"); } catch { }
    foreach (var line in File.ReadLines(args[1]))
    {
        var f = line.Split('\t');
        var sw = System.Diagnostics.Stopwatch.StartNew();
        var task = Task.Run(() => Reduce(f[0], f[1], f[2]));
        int rc = task.Wait(TimeSpan.FromSeconds(600)) ? task.Result : 124;
        Console.WriteLine(f[1] + "\t" + rc + "\t" + sw.Elapsed.TotalSeconds.ToString("F4"));
    }
    Environment.Exit(0);
}
return Reduce(args[0], args[1], args[2]);

static int Reduce(string src, string dst, string level)
{
    var r = new GdPicturePDFReducer();
    var c = r.PDFReducerConfiguration;
    c.PackDocument = true;
    c.PackFonts = true;
    c.RemoveMetadata = false;
    c.EnableMRC = false;
    c.OutputFormat = PDFReducerPDFVersion.PdfVersionRetainExisting;
    switch (level)
    {
        case "low":
            c.DownscaleResolution = 200;
            c.ImageQuality = PDFReducerImageQuality.ImageQualityHigh;
            break;
        case "medium":
            c.DownscaleResolution = 150;
            c.ImageQuality = PDFReducerImageQuality.ImageQualityMedium;
            break;
        case "high":
            c.DownscaleResolution = 72;
            c.ImageQuality = PDFReducerImageQuality.ImageQualityLow;
            break;
    }
    c.JBIG2PMSThreshold = 1.0f;
    return r.ProcessDocument(src, dst) == GdPictureStatus.OK ? 0 : 1;
}
