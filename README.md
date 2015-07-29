# Parse-iOS-Package
Update 2015-7-29
1. Fix bug that when desired icon not exist, it will goto forever loop.
2. Add feature, when desired icon not exist, while the desired index > 0, we will do index-- and check different size again.

Update 2015-7-28
1. Fix bug that with AppIcon60x60 in Info.plist, but can not find this image,
while we can find the AppIcon60x60@2x and AppIcon60x60@3x. 

Parse IPA package and get the information desired
