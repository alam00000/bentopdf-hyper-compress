import os
import sys

from pdftools_sdk.optimization.compression_algorithm_selection import CompressionAlgorithmSelection
from pdftools_sdk.optimization.optimizer import Optimizer
from pdftools_sdk.optimization.profiles.web import Web
from pdftools_sdk.pdf.document import Document
from pdftools_sdk.sdk import Sdk

LEVELS = {'low': (200, 0.8), 'medium': (150, 0.5), 'high': (72, 0.2)}

src, dst, level = sys.argv[1:4]
Sdk.initialize(os.environ['PDFTOOLS_KEY'])
dpi, quality = LEVELS[level]
profile = Web()
profile.resolution_d_p_i = dpi
profile.threshold_d_p_i = dpi * 1.5
profile.image_recompression_options.algorithm_selection = CompressionAlgorithmSelection.BALANCED
profile.image_recompression_options.compression_quality = quality
profile.removal_options.remove_metadata = False
profile.font_options.merge = True
with open(src, 'rb') as fh, Document.open(fh) as doc, open(dst, 'wb+') as out:
    with Optimizer().optimize_document(doc, out, profile):
        pass
