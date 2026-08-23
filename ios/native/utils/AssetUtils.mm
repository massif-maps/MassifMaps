#include "AssetUtils.h"
#include "core/BinaryData.h"
#include "utils/Log.h"

#import <Foundation/Foundation.h>

namespace massif {

    std::shared_ptr<BinaryData> AssetUtils::LoadAsset(const std::string& path) {
        // Convert std::string to NSString
        NSString* nsPath = [NSString stringWithUTF8String:path.c_str()];
        NSString* fileName = [nsPath stringByDeletingPathExtension];
        NSString* extension = [nsPath pathExtension];
        NSString* fullPath = [[NSBundle mainBundle] pathForResource:fileName ofType:extension];
        
        // Try to open file for reading
        NSFileHandle* fileHandle = [NSFileHandle fileHandleForReadingAtPath:fullPath];
        if (!fileHandle) {
            Log::Errorf("AssetUtils::LoadAsset: Asset not found: %s", path.c_str());
            return std::shared_ptr<BinaryData>();
        }
        
        // Read the file
        NSData* fileData = [fileHandle readDataToEndOfFile];
        [fileHandle closeFile];
        
        // Copy the data to vector
        std::vector<unsigned char> data;
        const unsigned char* bytes = static_cast<const unsigned char*>([fileData bytes]);
        data.assign(bytes, bytes + [fileData length]);
        return std::make_shared<BinaryData>(std::move(data));
    }

    namespace {
        /**
         * The bundle's resource directory joined with a relative asset path.
         *
         * Not pathForResource: that one takes a name and an extension and finds nothing for a
         * DIRECTORY, which is exactly what listing has to walk into.
         */
        NSString* BundlePath(const std::string& path) {
            NSString* root = [[NSBundle mainBundle] resourcePath];
            if (path.empty()) {
                return root;
            }
            return [root stringByAppendingPathComponent:[NSString stringWithUTF8String:path.c_str()]];
        }
    }

    bool AssetUtils::AssetExists(const std::string& path) {
        BOOL isDir = NO;
        BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:BundlePath(path)
                                                           isDirectory:&isDir];
        return exists && !isDir;
    }

    std::vector<std::string> AssetUtils::ListAssets(const std::string& path) {
        std::vector<std::string> names;
        NSArray<NSString*>* entries =
            [[NSFileManager defaultManager] contentsOfDirectoryAtPath:BundlePath(path) error:nil];
        for (NSString* entry in entries) {
            names.push_back(std::string([entry UTF8String]));
        }
        return names;
    }

    std::string AssetUtils::CalculateResourcePath(const std::string& resourceName) {
        NSString* nsResourceName = [NSString stringWithUTF8String:resourceName.c_str()];
        NSString* fileName = [nsResourceName stringByDeletingPathExtension];
        NSString* extension = [nsResourceName pathExtension];
        NSString* fullPath = [[NSBundle mainBundle] pathForResource:fileName ofType:extension];
        
        if (!fullPath) {
            Log::Errorf("AssetUtils::CalculateResourcePath: Asset not found: %s", resourceName.c_str());
            return std::string();
        }
        
        return std::string([fullPath UTF8String]);
    }
        
    std::string AssetUtils::CalculateWritablePath(const std::string& fileName) {
        NSString* nsFileName = [NSString stringWithUTF8String:fileName.c_str()];
        NSArray* dirPaths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString* docsDir = [dirPaths objectAtIndex:0];
        NSString* writablePath = [docsDir stringByAppendingPathComponent:nsFileName];
        return std::string([writablePath UTF8String]);
    }

    AssetUtils::AssetUtils() {
    }
    
}
