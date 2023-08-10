#include <array>
#include <iostream>
#include <cstring>
#include <unordered_map>
#include "crc32.h"
#include "zip_handler.h"

std::string CUnZipHandler::GetFileName(bool *isUTF8) {

    if ( !isValid )
        return {};

    if ( current == entries.end() )
        return {};

    auto centralDirectory = current->second.centralDirectory;

    if ( isUTF8 != nullptr )
        *isUTF8 = ( centralDirectory.flags & ( 1 << 11 ) ) != 0;

    return { current->second.fileName };
}

bool CUnZipHandler::IsDir()
{
    if ( !isValid )
        return false;

    if ( current == entries.end() )
        return {};

    auto centralDirectory = current->second.centralDirectory;

    auto len = centralDirectory.fileNameLength;
    if ( centralDirectory.uncompressedSize == 0 && len > 0 && current->second.fileName.ends_with('/') )
        return true;

    return false;
}

CUnZipHandler::Result CUnZipHandler::Read(std::vector<std::byte> &fileContents) {

    if ( !isValid )
        return Result::ZIPPER_RESULT_ERROR;

    if ( current == entries.end() )
    {
        return Result::ZIPPER_RESULT_ERROR;
    }

    if ( current->second.contentSize < 1 )
    {
        return Result::ZIPPER_RESULT_ERROR;
    }

    fileContents.assign(current->second.fileData.begin(), current->second.fileData.end());

    current++;

    if ( current == entries.end() )
    {
        return Result::ZIPPER_RESULT_SUCCESS_EOF;
    }

    return Result::ZIPPER_RESULT_SUCCESS;
}

bool CUnZipHandler::SkipFile()
{
    if ( !isValid )
        return false;

    if ( current == entries.end() )
        return false;
    current++;
    return true;
}

uint64_t CUnZipHandler::GetFileSize()
{
    if ( !isValid )
        return -1;

    if ( current == entries.end() )
        return 0;

    auto centralDirectory = current->second.centralDirectory;

    return centralDirectory.uncompressedSize;
}

CUnZipHandler::CUnZipHandler(std::byte* buff, int32_t size) : CZip(buff, size) {
    this->current = this->entries.begin();
    this->isValid = true;
}


CZipHandler::CZipHandler(std::byte *buff, uint32_t size) : CZip(buff, size)
{
    CCRC32::generate_table(table);
    this->isValid = true;
}

bool CZipHandler::AddBufferedFileToZip(const char *zfilename, const unsigned char *buf, size_t buflen)
{
    if(entries.size() + 1 >= MAX_ENTRIES)
        return false;

    ZipEntryContents entry;

    //local header
    LocalFileHeader header;
    header.compressionMethod = 0;
    header.flags = 8;
    header.version = 10;
    header.crc32 = CCRC32::update(table,0,buf,buflen);
    header.modificationTime = 0;
    header.modificationDate = 0;
    header.compressionSize = buflen;
    header.uncompressedSize = buflen;
    header.fileNameLength = strlen(zfilename);

    //central directory header
    CentralDirectory dir;
    dir.crc32 = CCRC32::update(table,0,buf,buflen);;
    dir.flags = 8;
    dir.uncompressedSize = buflen;
    dir.compressionSize = buflen;
    dir.fileNameLength = strlen(zfilename);;
    dir.commentLength = 0;
    dir.diskNumber = 0;
    dir.internalFileAttributes = 0;
    dir.externalFileAttributes = 0;

    //setting headers in entry
    entry.localHeader = header;
    entry.centralDirectory = dir;

    //internal information
    entry.contentSize = buflen;
    entry.fileData.reserve(buflen);
    entry.fileData.assign( (std::byte*)buf, (std::byte*)buf+buflen);
    entry.fileName = zfilename;

    //push to entries.
    entries.insert({zfilename,entry});

    return true;

}

void CZipHandler::GetZipFile(std::byte **buff, int *size)
{
    uint32_t totalSize = 0;
    uint32_t extraFilenameCountUp = 0;
    for(const auto& entry : entries) {
        totalSize += entry.second.fileData.size() + (entry.second.centralDirectory.fileNameLength) + entry.second.centralDirectory.commentLength + entry.second.centralDirectory.extraFieldLength;
        extraFilenameCountUp += (entry.second.centralDirectory.fileNameLength);
    }
    totalSize += ((sizeof(CentralDirectory) + sizeof(LocalFileHeader)) * entries.size()) + sizeof(EndOfCentralDirectory);
    totalSize += extraFilenameCountUp + this->globalComment.length();

    auto currentBuffer = const_cast<std::byte *>(*buff = new std::byte[totalSize]);

    int offset = 0;
    for(auto &entry : entries)
    {
        (&(&(&entry)->second)->centralDirectory)->RelativeOffsetLocalFileHeader = offset;
        memcpy(currentBuffer + offset, &entry.second.localHeader, sizeof(LocalFileHeader));
        offset += sizeof(LocalFileHeader);
        memcpy(currentBuffer + offset, entry.second.fileName.c_str(), strlen(entry.second.fileName.c_str()));
        offset += strlen(entry.second.fileName.c_str());
        if(entry.second.localHeader.extraFieldLength > 0)
        {
            memcpy(currentBuffer + offset, entry.second.localExtraFieldData.data(), entry.second.localHeader.extraFieldLength);
            offset+=entry.second.localHeader.extraFieldLength;
        }
        memcpy(currentBuffer + offset, entry.second.fileData.data(),entry.second.fileData.size());
        offset += entry.second.fileData.size();



    }

    uint32_t startOfCD = offset;
    uint32_t centralDirectorySize = 0;

    for(auto entry : entries)
    {
        memcpy(currentBuffer + offset, &entry.second.centralDirectory, sizeof(CentralDirectory));
        offset += sizeof(CentralDirectory);
        centralDirectorySize += sizeof(CentralDirectory);
        memcpy(currentBuffer + offset, entry.second.fileName.c_str(), entry.second.fileName.length());
        offset += entry.second.fileName.length();
        centralDirectorySize += entry.second.fileName.length();
        if(entry.second.centralDirectory.extraFieldLength > 0)
        {
            memcpy(currentBuffer + offset, entry.second.centralExtraFieldData.data(), entry.second.centralDirectory.extraFieldLength);
            offset+=entry.second.centralDirectory.extraFieldLength;
        }
        if(entry.second.centralDirectory.commentLength > 0)
        {
            memcpy(currentBuffer + offset, entry.second.centralComment.data(), entry.second.centralComment.length());
            offset+=entry.second.centralComment.length();
        }
    }

    EndOfCentralDirectory endOfCentralDirectory;
    endOfCentralDirectory.commentLength = this->globalComment.length();
    endOfCentralDirectory.numberOfThisDisk = 0;
    endOfCentralDirectory.diskWhereCentralDirectoryStarts = 0;
    endOfCentralDirectory.numberOfCentralDirectoryRecordsOnDisk = entries.size();
    endOfCentralDirectory.totalNumberOfCentralRecords = entries.size();
    endOfCentralDirectory.sizeOfCentralDirectory = centralDirectorySize;
    endOfCentralDirectory.offsetOfStartCentralDirectory = startOfCD;

    memcpy(currentBuffer + offset, &endOfCentralDirectory, sizeof(EndOfCentralDirectory));
    if(!this->globalComment.empty())
    {
        memcpy(currentBuffer + offset + sizeof(EndOfCentralDirectory), this->globalComment.c_str(), this->globalComment.length());
    }

    *size = totalSize;

}

bool CZipHandler::AddFileToZip(const char *filepath, const char *inZipName) {

    FILE* fl = fopen(filepath, "r");
    if(fl == nullptr)
        return false;

    fseek( fl, 0, SEEK_END );
    size_t size = ftell( fl );
    unsigned char* fileContents = new unsigned char[size];

    rewind( fl );
    fread( fileContents, sizeof( char ), size, fl );

    fclose( fl );

    bool success = AddBufferedFileToZip(inZipName ? inZipName : filepath, fileContents, size);;

    delete[] fileContents;

    return success;
}

bool CZipHandler::RemoveFileFromZip(const char *filepath) {
    return entries.erase(filepath) != 0;
}

CZip::CZip(std::byte *buff, uint32_t size)
{
    std::unordered_map<int, ZipEntryContents> existingEntries;
    for(int i = 0; i < size; i++)
    {
        if(*reinterpret_cast<int*>(buff + i) == 0x04034b50)
        {
            ZipEntryContents contents{0};
            auto eocdTest = reinterpret_cast<LocalFileHeader*>(buff + i);
            memcpy(&contents.localHeader,eocdTest, sizeof(LocalFileHeader));
            contents.fileName.reserve(eocdTest->fileNameLength );
            contents.fileName.assign(reinterpret_cast<char*>(buff) + i + sizeof(LocalFileHeader), reinterpret_cast<char*>(buff) + i + sizeof(LocalFileHeader) + eocdTest->fileNameLength);
            int offsetSize = contents.fileName.length() + (i + sizeof(LocalFileHeader));

            if(eocdTest->extraFieldLength > 0) {
                contents.localExtraFieldData.reserve(eocdTest->extraFieldLength);
                contents.localExtraFieldData.assign((std::byte *) buff + offsetSize,
                                                    (std::byte *) buff + offsetSize + eocdTest->extraFieldLength);
            }
            offsetSize += eocdTest->extraFieldLength;
            contents.fileData.reserve(eocdTest->uncompressedSize);
            contents.fileData.assign( (std::byte*)buff + offsetSize, (std::byte*)buff + offsetSize + eocdTest->uncompressedSize);

            contents.contentSize = eocdTest->uncompressedSize;
            existingEntries.insert({i, contents});
            continue;
        }

        if(*reinterpret_cast<int*>(buff + i) == 0x02014b50)
        {
            auto eocdTest = reinterpret_cast<CentralDirectory*>(buff + i);
            auto contents = existingEntries[eocdTest->RelativeOffsetLocalFileHeader];//.extract(eocdTest->RelativeOffsetLocalFileHeader);

            memcpy(&contents.centralDirectory,eocdTest, sizeof(CentralDirectory));

            int offsetSize = contents.fileName.length() + (i + sizeof(LocalFileHeader));

            if(eocdTest->extraFieldLength > 0) {
                contents.centralExtraFieldData.reserve(eocdTest->extraFieldLength);
                contents.localExtraFieldData.assign((std::byte *) buff + offsetSize,
                                                    (std::byte *) buff + offsetSize + eocdTest->extraFieldLength);
            }

            offsetSize += eocdTest->extraFieldLength;
            if(eocdTest->commentLength > 0) {
                contents.centralComment.reserve(eocdTest->extraFieldLength);
                contents.centralComment.assign((char *) buff + offsetSize,
                                               (char *) buff + offsetSize + eocdTest->extraFieldLength);
            }

            entries.insert({contents.fileName,contents});
            continue;
        }
        if(*reinterpret_cast<int*>(buff + i) == 0x06054b50) {
            auto eocdTest = reinterpret_cast<EndOfCentralDirectory *>(buff + i);
            this->globalComment.reserve( eocdTest->commentLength );
            this->globalComment.assign(reinterpret_cast<char*>(buff) + i + sizeof(EndOfCentralDirectory), reinterpret_cast<char*>(buff) + i + sizeof(EndOfCentralDirectory) + eocdTest->commentLength);
            continue;
        }
    }
}

void CZip::useExistingZipEntries(const CZip & oldZip) {
        entries.insert(oldZip.entries.begin(), oldZip.entries.end());
}
