# For secret loot that shouldn't be versioned
if [[ -a ~/.localrc ]]
then
    source ~/.localrc
fi

export PATH="~/dotfiles/bin:/usr/local/bin:/usr/local/sbin:$PATH"
export MANPATH="/usr/local/man:/usr/local/git/man:$MANPATH"


### Terminal colors, prompt, etc

source "`brew --prefix`/etc/grc.bashrc"

colored_printf () {
    printf "\001\033[0;$1m\002$2\001\033[0m\002"
}

function git_branch {
    # http://stackoverflow.com/a/2185353
    if git rev-parse --git-dir > /dev/null 2>&1
    then
        # http://unix.stackexchange.com/a/155077
        if [ -z "$(git status --porcelain)" ]
        then # Working directory clean
          local color="32" # green
        else # Uncommitted changes
          local color="31" # red
        fi

        if branch=$(git symbolic-ref --short -q HEAD)
        then
            colored_printf "$color" " $branch"
        else
            colored_printf "$color" " (no branch)"
        fi
    fi
}

export PS1="[\w\$(git_branch)]\$ "

### Pyenv

# makes loading slow... do this in the background? is that possible?
# eval "$(pyenv init -)"


### Java

# used by .dotfiles/aws
# export JAVA_HOME=$(/usr/libexec/java_home)
# export PATH=/Library/Java/JavaVirtualMachines/jdk1.8.0_45.jdk/Contents/Home/bin:$PATH


### AWS

export AWS_HOME=~/.aws


# EC2   
export EC2_HOME=$AWS_HOME/ec2
export PATH=$PATH:$EC2_HOME/bin
#export EC2_URL=https://ec2.us-west-1.amazonaws.com

# ELB
export AWS_ELB_HOME=$AWS_HOME/elb
export PATH=$PATH:$AWS_ELB_HOME/bin
#export AWS_ELB_URL=https://elasticloadbalancing.us-west-1.amazonaws.com

export EC2_PRIVATE_KEY=`ls $AWS_HOME/pk-*.pem`
export EC2_CERT=`ls $AWS_HOME/cert-*.pem`


### Git

alias gs='git status'
alias gf='git fetch'
alias glog="git log --graph --pretty=format:'%Cred%h%Creset %an: %s - %Creset %C(yellow)%d%Creset %Cgreen(%cr)%Creset' --abbrev-commit --date=relative"
alias gp='git push origin HEAD'
alias gd='git diff'
alias gds='git diff --staged'
alias gc='git commit'
alias gca='git commit -a'
alias gco='git checkout'
alias gb='git branch'
alias gg='git grep'
alias gbr="git for-each-ref --sort=-committerdate refs/heads/ --format='%(refname) %(committerdate) %(authorname)' | sed 's/refs\/heads\///g'"
alias gdmb='git branch --merged | grep -v "\*" | xargs -n 1 git branch -d'


### General conveniences

function p() {
    cd "$HOME/Projects/$1"
}

alias subl='"/Applications/Sublime Text.app/Contents/SharedSupport/bin/subl"'
alias setup='~/dotfiles/setup'
alias pubkey="more ~/.ssh/id_rsa.pub | pbcopy | echo '=> Public key copied to pasteboard.'"

# Usage: extract <file>
# Description: extracts archived files / mounts disk images
# Note: .dmg/hdiutil is Mac OS X-specific.
extract () {
    if [ -f $1 ]; then
        case $1 in
            *.tar.bz2)  tar -jxvf $1                        ;;
            *.tar.gz)   tar -zxvf $1                        ;;
            *.bz2)      bunzip2 $1                          ;;
            *.dmg)      hdiutil mount $1                    ;;
            *.gz)       gunzip $1                           ;;
            *.tar)      tar -xvf $1                         ;;
            *.tbz2)     tar -jxvf $1                        ;;
            *.tgz)      tar -zxvf $1                        ;;
            *.zip)      unzip $1                            ;;
            *.ZIP)      unzip $1                            ;;
            *.pax)      cat $1 | pax -r                     ;;
            *.pax.Z)    uncompress $1 --stdout | pax -r     ;;
            *.Z)        uncompress $1                       ;;
            *)          echo "'$1' cannot be extracted/mounted via extract()" ;;
        esac
    else
        echo "'$1' is not a valid file"
    fi
}

### Autocompletion

bind "set completion-ignore-case on"
bind "set show-all-if-ambiguous on"

#if [ -f $(brew --prefix)/etc/bash_completion ]; then
    #. $(brew --prefix)/etc/bash_completion
#fi
