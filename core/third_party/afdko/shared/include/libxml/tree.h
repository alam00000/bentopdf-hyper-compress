/* Hyper overlay: minimal libxml/tree.h stub.  Upstream AFDKO's
 * shared/include/ctlshare.h + tx_shared.h pull libxml in for the
 * xml_read callback used by the SVG / UFO reader modules — both
 * of which we strip from the vendored AFDKO subset.  This stub
 * provides just enough typedef coverage to make ctlshare.h
 * compile without pulling actual libxml2 into our dylib. */
#ifndef HYPER_LIBXML_TREE_H_STUB_
#define HYPER_LIBXML_TREE_H_STUB_
typedef struct _xmlDoc *xmlDocPtr;
typedef struct _xmlNode *xmlNodePtr;
typedef struct _xmlAttr *xmlAttrPtr;
typedef unsigned char xmlChar;
#endif
