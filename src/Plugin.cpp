/*
 * =====================================================================================
 *
 *       Filename:  Plugin.cpp
 *
 *    Description:  Main Plugin class
 *
 *        Version:  1.0
 *        Created:  05/06/2015 03:06:36 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Mikhail Savochkin (msavochkin@critical-factor.com), 
 *
 * =====================================================================================
 */

#include "Plugin.h"

const std::string Plugin::GIT_DIR       = ".git";
const std::string Plugin::GIT_CONFIG    = ".git/keshig";
const std::string Plugin::GIT_CACHE_DIR = ".git/bin-cache";
const std::string Plugin::GIT_BIN_INDEX = ".git/bin-index";
const std::string Plugin::SEPARATOR = "<--->";

Plugin::Plugin() : _terminate(false)
{

}

Plugin::~Plugin()
{
    
}

void Plugin::initialize(Application& self)
{
    Application::initialize(self);
    //if (!_terminate) setupLogger();
}

void Plugin::uninitialize()
{
    Application::uninitialize();
}

void Plugin::setupLogger()
{
    AutoPtr<FileChannel>            pChannel(new FileChannel);
    AutoPtr<ConsoleChannel>         cConsole(new ConsoleChannel);
    AutoPtr<SplitterChannel>        pSplitter(new SplitterChannel);
    AutoPtr<PatternFormatter>       pFormatter(new PatternFormatter);
    pFormatter->setProperty("pattern", "%Y-%m-%d %H:%M:%S [%p]: %t");
    pFormatter->setProperty("times", "local");
    // We will uncomment this later. Maybe. This preventing from writing into log files
    //pSplitter->addChannel(pChannel);
    pSplitter->addChannel(cConsole);
    //pChannel->setProperty("path", "/var/log/git-keshig.log");
    //pChannel->setProperty("rotation", "5 M");
    AutoPtr<FormattingChannel> pFormatChannel(new FormattingChannel(pFormatter, pSplitter));
    Logger& log = Application::instance().logger();
    log.setLevel(Poco::Message::PRIO_DEBUG);
    log.setChannel(pFormatChannel);
}

void Plugin::defineOptions(OptionSet& options)
{
//    ServerApplication::defineOptions(options);
    options.addOption(
            Option("help", "h", "display argument help information")
            .required(false)
            .repeatable(false)
            .callback(OptionCallback<Plugin>(
                    this, &Plugin::handleHelp)));

    options.addOption(
            Option("init", "", "initialize keshig subsystem in current directory")
            .required(false)
            .repeatable(false)
            .argument("ssh url")
            .callback(OptionCallback<Plugin>(
                    this, &Plugin::handleInit)));
    
    options.addOption(
            Option("check", "c", "iterates all subdirectories and checks for binary and large files")
            .required(false)
            .repeatable(false)
            .callback(OptionCallback<Plugin>(
                    this, &Plugin::handleCheck)));
    
    options.addOption(
            Option("list", "l", "lists all files that is tracked by keshig")
            .required(false)
            .repeatable(false)
            .callback(OptionCallback<Plugin>(
                    this, &Plugin::handleList)));

    options.addOption(
            Option("add", "", "adds file into repository")
            .required(false)
            .repeatable(false)
            .argument("filepath")
            .callback(OptionCallback<Plugin>(
                    this, &Plugin::handleAdd)));
}

void Plugin::addFile(const std::string filepath)
{
    // Check if file exists and it's not a directory
    File file(filepath);
    if (!file.exists())
    {
        std::cout << "Failed to add file " << filepath.c_str() << ": file does not exist" << std::endl;
        return;
    }
    if (file.isDirectory())
    {
        std::cout << "Failed to add file " << filepath.c_str() << ": file is a directory" << std::endl;
        return;

    }
    File index(Plugin::GIT_BIN_INDEX);
    if (!index.exists())
    {
        index.createFile();
    }
    // Find this file in the index
    if (isFileIndexed(filepath))
    {
        // Not fresh. Update existing index file
    }
    // Add new file into index
    UUIDGenerator gen;
    IndexEntry e;
    e.filepath = filepath;
    e.md5 = getFileMd5(filepath);
    e.uuid = gen.createRandom().toString();
    do 
    {
        e.uuid = gen.createRandom().toString();
    }
    while (!isUuidUnique(e.uuid));
    _index.push_back(e);
    writeIndex();
}

void Plugin::writeIndex()
{
    Poco::FileOutputStream ostr(Plugin::GIT_BIN_INDEX);
    std::string buffer;
    for (auto it = _index.begin(); it != _index.end(); it++)
    {
        buffer.append((*it).filepath);
        buffer.append(Plugin::SEPARATOR);
        buffer.append((*it).md5);
        buffer.append(Plugin::SEPARATOR);
        buffer.append((*it).uuid);
        buffer.append("\n");
    }
    ostr << buffer;
}

bool Plugin::isUuidUnique(const std::string uuid)
{
    for (auto it = _index.begin(); it != _index.end(); it++)
    {
        if ((*it).uuid == uuid) return false;
    }
    return true;
}

std::string Plugin::getFileMd5(const std::string filepath) const
{
    Poco::FileInputStream fstr(filepath);
    MD5Engine md5;
    DigestOutputStream ostr(md5);
    Poco::StreamCopier::copyStream(fstr, ostr);
    ostr.flush();
    const DigestEngine::Digest& digest = md5.digest();
    return DigestEngine::digestToHex(digest);
}

bool Plugin::isFileIndexed(const std::string filepath)
{
    readIndex();
    for (auto it = _index.begin(); it != _index.end(); it++)
    {
        if ((*it).filepath == filepath) return true;
    }
    return false;
}

void Plugin::readIndex()
{
    File f(Plugin::GIT_BIN_INDEX);
    if (!f.exists()) f.createFile();
    _index.clear();
    FileInputStream fstr(f.path());
    std::string buffer;
    Poco::StreamCopier::copyToString(fstr, buffer);
    StringTokenizer lines(buffer, "\n");
    for (auto line = lines.begin(); line != lines.end(); line++)
    {
        StringTokenizer parts((*line), "<--->");
        try 
        {
            IndexEntry e;
            e.filepath = parts[0];
            e.md5 = parts[1];
            e.uuid = parts[2];
        } 
        catch (Poco::Exception e)
        {
            
        }
    }
}

void Plugin::handleHelp(const std::string& name, const std::string& value)
{
    HelpFormatter helpFormatter(options());
    helpFormatter.setCommand(commandName());
    helpFormatter.setUsage("OPTIONS");
    helpFormatter.setHeader(
            "git-bin plugin for git: Help with tracking of large files in git");
    helpFormatter.format(std::cout);
    stopOptionsProcessing();
    _terminate = true;
}

void Plugin::handleAdd(const std::string& name, const std::string& value)
{
    LogStream lstr(Application::instance().logger());
    lstr.information() << "Checking path" << std::endl;
    if (!hasGitDirectory()) 
    {
        lstr.error() << "This is not a git repository" << std::endl;
        return;
    }    
    File f(value);
    if (!f.exists())
    {
        lstr.error() << value << " file does not exist" << std::endl;
        // TODO: Remove this file from index
        return;
    }
    bool isGitable = true;
    if (f.isDirectory())
    {
        lstr.information() << "Can't add directory" << std::endl;
        return;
        // We need to process every that is not tracked
    }
    if (f.isDevice())
    {
        isGitable = false; 
    }
    if (f.isLink())
    {
        isGitable = false;
    }
    Poco::Pipe out, err;
    Process::Args args;
    args.push_back("status");
    args.push_back(value);
    args.push_back("-s");
    Poco::ProcessHandle* git = new Poco::ProcessHandle(Poco::Process::launch("git", args, 0, &out, &err));
    PipeInputStream str(out);
    std::string output;
    Poco::StreamCopier::copyToString(str, output);
    StringTokenizer st(output, " ");
    output = trim(output);
    for (auto it = st.begin(); it != st.end(); it++)
    {
        if ((*it).empty())
        {
            continue;
        }
        if ((*it) == "M")
        {
            std::cout << "Modified" << std::endl;
            replaceWithLink(value);
            // Check if file is already in index - we may want to to update binary file 
        }
        else if ((*it) == "??")
        {
            std::cout << "Not indexed" << std::endl;
            replaceWithLink(value);
        }
        else
        {
            std::cout << (*it).c_str() << std::endl;
        }
    }
}

int Plugin::replaceWithLink(std::string filepath)
{
    std::cout << "Replacing with link " << filepath.c_str() << std::endl;
    
    std::ifstream fstr(filepath.c_str());
    MD5Engine md5;
    DigestOutputStream ostr(md5);
    Poco::StreamCopier::copyStream(fstr, ostr);
    ostr.flush();
    const DigestEngine::Digest& digest = md5.digest();
    std::string sourceMd5 = DigestEngine::digestToHex(digest);
    std::cout << "File contents MD5 sum: " << sourceMd5.c_str() << std::endl;

    // Generate new file name
    UUIDGenerator gen;
    UUID tmpUuid = gen.createRandom();
    std::string uuid = tmpUuid.toString(); 
    std::string newFile(Plugin::GIT_CACHE_DIR);
    newFile.append("/");
    newFile.append(uuid);

    Process::Args args;
    args.push_back(filepath);
    args.push_back(newFile);
    Poco::ProcessHandle ph = Process::launch("mv", args, 0, 0, 0);

    // Failback with sudo
    if (ph.wait() != 0)
    {
        args.clear();
        args.push_back("mv");
        args.push_back(filepath);
        args.push_back(newFile);
        ph = Process::launch("sudo", args, 0, 0, 0);
    }

    // Check if file was moved
    File originalFile(filepath);
    if (originalFile.exists())
    {
        std::cout << "Failed to move original file" << std::endl;
        return -1;
    }

    return 1;
}

void Plugin::handleStatus(const std::string& name, const std::string& value)
{

}

void Plugin::handleList(const std::string& name, const std::string& value)
{
    
}

void Plugin::handleCheck(const std::string& name, const std::string& value)
{

}

void Plugin::handleInit(const std::string& name, const std::string& value)
{
    if (!hasGitDirectory())
    {
        logger().error("Can't initialize keshig: not a git repository");
        return;
    }
    logger().information("Initializing Keshig");
    File f(Plugin::GIT_CONFIG);
    if (f.exists())
    {
        logger().error("This repo already has keshig configuration");
        return;
    }
    f.createFile();
    AutoPtr<PropertyFileConfiguration> config = new PropertyFileConfiguration(); 
    config->setString("url", value);  
    config->save(f.path());
    File dir(Plugin::GIT_CACHE_DIR);
    dir.createDirectory();
}

void Plugin::handleSync(const std::string& name, const std::string& value)
{
    
}

int Plugin::main(const std::vector<std::string>& args)
{
    return Application::EXIT_OK;
}

bool Plugin::hasGitDirectory()
{
    File f(Plugin::GIT_DIR);
    return f.exists();
}
