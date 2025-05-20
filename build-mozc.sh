#!/bin/zsh -eu

PROJECT=$(dirname "$(realpath $0)")
cd $PROJECT

declare -A env=()
declare -A opts=()
declare -A stats=()

## configuration

declare -A build_bool_opts=(
    [setup]=false
    [for-win]=false
    [update]=false
    [clean]=false
    [emacs]=true
    [install]=false
)

declare -A build_value_opts=(
    [win-workspace]=mozc-workspace
)

declare -A dict_opts=(
    [alt-cannadic]=true
    [edict2]=true
    [jawiki]=true
    [neologd]=true
    [personal-names]=true
    [place-names]=true
    [skk-jisyo]=true
    [sudachidict]=true
)

for k in ${(@k)build_bool_opts}; do
    opts[$k]=${build_bool_opts[$k]}
done
for k in ${(@k)build_value_opts}; do
    opts[$k]="${build_value_opts[$k]}"
done
for k in ${(@k)dict_opts}; do
    opts[$k]=${dict_opts[$k]}
done

local nodejs_version=v22

alias win_cmd="/mnt/c/Windows/System32/cmd.exe /c"
alias win_dev_cmd='/mnt/c/Windows/System32/cmd.exe /c call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" \&'
local this_script=$(basename $0)

usage() {
    print -rC1 -- \
          "" \
          "Usage:" \
          "    $this_script:t <-h|--help>            Show this  help" \
          "    $this_script:t [build options...]     Build mozc" \
          "" \
          "build options:" \
          "    --setup, --no-setup                    Install & Setup build tools & libraraies. Default: $opts[setup]" \
          "    --for-win, --no-for-win                Build mozc for windows. This option is only aveilable under WSL. Default: $opts[for-win]" \
          "    --update, --no-update                  Sync with remote git repositories. Default: $opts[update]" \
          "    --clean, --no-clean                    Clean build. Default: $opts[clean]" \
          "    --install, --no-install                Install mozc after build task. Default: $opts[install]" \
          "    --win-workspace=<dir>                  Location for build workspace for windows. Default: $opts[win-workspace]" \
          "                                           Relative path from USERPROFILE directory." \
          "    --alt-cannadic, --no-alt-cannadic      Enable additional alt-canna dictionary. Default: $opts[alt-cannadic]" \
          "    --edict2, --no-edict2                  Enable additional edict2 dictionary. Default: $opts[edict2]" \
          "    --jawiki, --no-jawiki                  Enable additional jawiki dictionary. Default: $opts[jawiki]" \
          "    --neologd, --no-neolog                 Enable additional neolog dictionary. Default: $opts[neologd]" \
          "    --personal-names, --no-personal-names  Enable additional alt-canna dictionary. Default: $opts[personal-names]" \
          "    --place-names, --no-place-names        Enable additional place-names dictionary. Default: $opts[place-names]" \
          "    --skk-jisyo, --no-skk-jisyo            Enable additional skk-jisyo dictionary. Default: $opts[skk-jisyo]" \
          "    --sudachidict, --no-sudachidict        Enable additional sudachidict dictionary. Default: $opts[sudachidict]"
    exit 0
}

# options
# -----------------------------------
cmd_opts() {
}

win_env() {
    win_cmd echo "%$1%" 2>/dev/null | tr -d '\r'
}

log() {
    local timestamp=$(date "+%Y-%m-%d %H:%M:%S.%3N")
    print -P "%F{green}[$timestamp]%f $*"
}

error_exit() {
    local timestamp=$(date "+%Y-%m-%d %H:%M:%S.%3N")
    print -P "%F{red}[$timestamp] Error:%f $*"
    exit 1
}

repo() {
    local basedir=$1
    local repo=$2
    local update=$3
    local clean=$4

    local local_repo=$basedir/$(basename $repo)
    local revision_file=$PROJECT/revs/${repo/\//_}
    local has_changed=false

    if [[ -d $local_repo ]]; then
        if $update; then
            cd $local_repo

            local current_revision=$(git rev-parse HEAD)

            log "synching local repo with remote repo [$repo]"
            git reset --hard --recurse-submodules

            if $clean; then
                git clean -dfx
                git submodule foreach --recursive git clean -dfx
            fi
            git pull --rebase --depth=1 --update-shallow --recurse-submodules


            local new_revision=$(git rev-parse HEAD)

            if [[ $current_revision != $new_revision ]]; then
                log "[$repo] has changed. [$current_revision] -> [$remote_revision]"
                echo $new_revision > $revision_file
                has_changed=true
            fi

            cd -
        fi
    else
        log "cloning remote repos itory [$repo]"
        pushd $basedir
        git clone --depth=1 --single-branch --recurse-submodules --shallow-submodules  https://github.com/${repo}.git
        has_changed=true
        pushd $local_repo
        git rev-parse HEAD > $revision_file
        popd
        popd
    fi
    $has_changed
}

fedora_pkgs() {
    log "installing fedora packages"

    sudo dnf update -y --no-best
    sudo dnf install -y \
         glib2-devel \
         ibus-devel \
         qt6-qtbase-devel
    sudo dnf autoremove
    sudo dnf clean all
}

macos_pkgs() {
}

win_pkgs() {
}

install_os_packages() {
    if [[ $os == linux ]]; then
        install_${distro}_packages
    else
        install_${os}_packages
    fi
}

node_env() {
    log "activating nodejs"
    if $opts[for-win]; then
        cd $stats[win-workspace]
        win_cmd nvm use $nodejs_version
        if [[ ! -f node_modules ]]; then
            cp $PROJECT/package.json .
            win_cmd npm install
        fi
        cd -
    else
        source $NVM_DIR/nvm.sh
        cd $PROJECT
        nvm use $nodejs_version
        if [[ ! -f node_modules ]]; then
            npm install
        fi
        cd -
    fi
}

py_venv() {
    log "activating python venv"

    cd $PROJECT
    if [[ ! -d .venv ]]; then
        python3 -m venv .venv
        source .venv/bin/activate
        pip install --upgrade pip
        pip install -r requirements.txt
    else
        source .venv/bin/activate
    fi
    cd -
}

install_node_packages() {
    log "installing node packages"

    cd $PROJECT
    npm install
}

activate_direnv() {
    log "activating direnv"

    cd $PROJECT
    direnv allow
    eval "$(direnv export zsh)"
}


baxel_error() {
    if $ops[for-win]; then
        cd $stats[win-workspace]/mozc/src
        win_cmd npx bazel shutdown
    else
        cd $PROJECT/repos/mozc/src
        npx bazel shutdown
    fi
    exit 1
}

env() {
    case $(uname -o) in
        GNU/Linux )
            env[os]=linux
            if [[ -f /etc/fedora-release ]]; then
                env[distro]=fedora
            fi
            if [[ -v WSL_INTEROP ]]; then
                env[wsl]=true
            fi
            ;;
        Darwin )
            env[os]=macos
            ;;
    esac
    env[arch]=$(uname -m)
    if [[ -n $env[(i)wsl] ]] && $env[wsl]; then
        env[win-home]=$(wslpath $(win_env USERPROFILE))
    fi
}

opts() {
    local -A cmd_opts=()
    local aveilable_opts=()

    # override default options
    if [[ -f $PROJECT/.config ]]; then
        source $PROJECT/.config
    fi

    # parse command line options
    for o in ${(k)build_bool_opts}; do
        aveilable_opts+=(-$o -no-$o)
    done
    for o in ${(k)dict_opts}; do
        aveilable_opts+=(-$o -no-$o)
    done
    for o in ${(k)build_value_opts}; do
        aveilable_opts+=(-$o:)
    done

    zparseopts -D -E -F -A cmd_opts -- \
               h -help $aveilable_opts[*]

    for o in ${(@k)cmd_opts}; do
        if [[ $o == -h || $o == --help ]]; then
            usage
        elif [[ $o =~ ^--no- ]]; then
            key=${o:5}
            opts[$key]=false
        else
            key=${o:2}
            if [[ -v build_value_opts[$key] ]]; then
                opts[$key]=${cmd_opts[$o]/#=/}
            else
                opts[$key]=true
            fi
        fi
    done
}

init() {
    if [[ -z ${env[(i)wsl]} ]] && $opts[for-win]; then
        error_exit "--for-win option is only available on WSL"
    fi

    for dic in ${(@k)dict_opts}; do
        if $opts[$dic]; then
            stats[ut]=true
            break
        fi
    done


    mkdir -p $PROJECT/revs
    mkdir -p $PROJECT/repos
    mkdir -p $PROJECT/dist

    if $opts[for-win]; then
        stats[win-workspace]=$env[win-home]/$opts[win-workspace]
        mkdir -p $stats[win-workspace]
        mkdir -p $stats[win-workspace]/dist
    fi
    py_venv
    node_env
}

repos() {
    if repo $PROJECT/repos \
            utuhiro78/merge-ut-dictionaries \
            $opts[update] $opts[clean]; then
        stats[ut-changed]=true
    fi
    for dic in ${(@k)dict_opts}; do
        if $opts[$dic]; then
            if repo $PROJECT/repos \
                    utuhiro78/mozcdic-ut-${dic} \
                    $opts[update] $opts[clean]; then
                stats[ut-changed]=true
            fi
        fi
    done

    local mozc_base_dir=$PROJECT/repos
    if $opts[for-win]; then
        mozc_base_dir=$stats[win-workspace]
    fi

    if repo $mozc_base_dir \
            google/mozc \
            $opts[update] $opts[clean]; then
        stats[mozc-changed]=true

        cd $mozc_base_dir/mozc
        local mozc_deps=$(git hash-object -w src/build_tools/update_deps.py)
        if [[ ! -f $PROJECT/revs/mozc_deps ]] \
               || [[ $mozc_deps != $(cat $PROJECT/revs/mozc_deps) ]]; then
            stats[mozc-deps-changed]=true
            echo $mozc_deps > $PROJECT/revs/mozc_deps
        fi
        cd -
    fi
}

ut() {
    local temp_mozcdic_ut_txt=$PROJECT/repos/merge-ut-dictionaries/src/merge/mozcdic-ut.txt
    local dist_mozcdic_ut_txt=$PROJECT/dist/mozcdic-ut.txt

    # without ut dictionary
    if [[ -z $stats[(i)ut] ]]; then
        return
    fi

    if [[ -f $dist_mozcdic_ut_txt ]] ; then
        # without update
        if ! $opts[update]; then
            return
        fi
        if [[ -z $stats[(i)ut-changed] ]]; then
            log "mozcdic-ut.txt is up to date"
            return
        fi
    fi
    log "building mozcdic-ut.txt"

    rm -f $temp_mozcdic_ut_txt

    for dic in ${(@k)dict_opts}; do
        log $dic
        if $opts[$dic]; then
            log "merging [mozcdic-ut-${dic}.txt]"
            bzcat $PROJECT/repos/mozcdic-ut-${dic}/mozcdic-ut-${dic}.txt.bz2 >> $temp_mozcdic_ut_txt
        fi
    done

    log "applying merge_dictionaries.py"
    cd $PROJECT/repos/merge-ut-dictionaries/src/merge
    ls -l mozcdic-ut.txt
    python merge_dictionaries.py mozcdic-ut.txt
    ls -l mozcdic-ut.txt
    cd -
    mv -f $temp_mozcdic_ut_txt $dist_mozcdic_ut_txt
}

macos_mozc() {
    cd $PROJECT/repos/mozc/src

    if $opts[clean]; then
        log "cleanup mozc buid tree"
        npx bazel clean --expunge
        python3 build_mozc.py clean
    fi

    if [[ ! -d third_party/qt_src || -n $stats[(i)mozc-deps-changed] ]]; then
        log "updating mozc dependencies"
        python3 build_tools/update_deps.py
    fi

    # TODO build with homebrew Qt6
    if [[ ! -d third_party/qt || ! -f $PROJECT/revs/qt ]] \
           || ! cmp -s $PROJECT/revs/qt third_party/qt_src/.tag; then
        log "building qt"
        python3 build_tools/build_qt.py --release --confirm_license
        cp -f third_party/qt_src/.tag $PROJECT/revs/qt
    else
        log "qt is up to date"
    fi

    git checkout data/dictionary_oss/dictionary00.txt
    if [[ -n $stats[(i)ut] ]]; then
        log "merging modzdic-ut into mozc"
        ls -l data/dictionary_oss/dictionary00.txt
        cat $PROJECT/dist/mozcdic-ut.txt >> data/dictionary_oss/dictionary00.txt
        ls -l data/dictionary_oss/dictionary00.txt
    fi

    local bazel_targets=(package)
    if $opts[emacs]; then
        bazel_targets+=(//unix/emacs:mozc_emacs_helper)
    fi

    log "start bazel build task"
    MOZC_QT_PATH=${PWD}/third_party/qt npx bazel build $bazel_targets[*] \
                --config oss_macos \
                --config release_build \
        || bazel_error
    npx bazel shutdown

    cp -f bazel-bin/mac/Mozc.pkg $PROJECT/dist
    if $opts[emacs]; then
        cp -f bazel-bin/unix/emacs/mozc_emacs_helper $PROJECT/dist
    fi

    cd -

    if $opts[install]; then
        log "installing mozc"
        if $opts[emacs]; then
            cp -f $PROJECT/dist/mozc_emacs_helper ~/.local/bin
        fi
        sudo installer -pkg $PROJECT/dist/Mozc.pkg -target /
        log "installation finished successfully"
    fi
}

linux_mozc() {
    cd  $PROJECT/repos/mozc/src

    if $opts[clean]; then
        log "cleanup bazel cache"
        npx bazel clean --expunge
        python3 build_mozc.py clean
    fi

    git checkout data/dictionary_oss/dictionary00.txt
    if [[ -n $stats[(i)ut] ]]; then
        log "merging modzdic-ut into mozc"
        ls -l data/dictionary_oss/dictionary00.txt
        cat $PROJECT/dist/mozcdic-ut.txt >> data/dictionary_oss/dictionary00.txt
        ls -l data/dictionary_oss/dictionary00.txt
    fi
    log "start bazel build task"

    npx bazel build package --config oss_linux --config release_build \
        || bazel_error
    npx bazel shutdown
    log "bazel task finished successfully"

    cp  -f bazel-bin/unix/mozc.zip $PROJECT/dist

    cd -

    if $opts[install]; then
        log "installing mozc"
        sudo unzip -o $PROJECT/dist/mozc.zip "usr/*" -d /
        log "installation finished successfully"
    fi
}


win_mozc() {
    cd $stats[win-workspace]/mozc/src

    if $opts[clean]; then
        log "cleanup mozc buid tree"
        win_cmd npx bazel clean --expunge
        win_cmd python3 build_mozc.py clean
    fi

    if [[ ! -d third_party/llvm || -n $stats[(i)mozc-deps-changed] ]]; then
        log "updating mozc dependencies"
        win_cmd python3 build_tools/update_deps.py
    fi

    if [[ ! -d third_party/qt ]] \
           || [[ ! -f $PROJECT/revs/qt ]] \
           || ! cmp -s $PROJECT/revs/qt third_party/qt_src/.tag; then
        log "building qt"
        win_cmd python3 build_tools/build_qt.py --release --confirm_license
        cp -f third_party/qt_src/.tag  $PROJECT/revs/qt
    fi

    git checkout data/dictionary_oss/dictionary00.txt
    if [[ -n $stats[(i)ut] ]]; then
        log "merging modzdic-ut into mozc"
        ls -l data/dictionary_oss/dictionary00.txt
        cat $PROJECT/dist/mozcdic-ut.txt >> data/dictionary_oss/dictionary00.txt
        ls -l data/dictionary_oss/dictionary00.txt
    fi

    log "start bazel build task"
    local bazel_targets=(package)
    if $opts[emacs]; then
        bazel_targets+=(//unix/emacs:mozc_emacs_helper)
    fi
    win_cmd npx bazel build $bazel_targets[*] \
            --config oss_windows \
            --config release_build \
        || bazel_error
    win_cmd npx bazel shutdown

    if $opts[emacs]; then
        cp -f bazel-bin/unix/emacs/mozc_emacs_helper.exe $stats[win-workspace]/dist
    fi
    cp -f bazel-bin/win32/installer/* $stats[win-workspace]/dist

    cd -

    if $opts[install]; then
        cd $stats[win-workspace]/dist
        log "installing mozc"
        if $opts[emacs]; then
            cp -f mozc_emacs_helper.exe $env[win-home]/.local/bin
        fi
        win_cmd start /wait msiexec /i Mozc64.msi
        log "installation finished successfully"
        cd -
    fi
}

env
opts "$@"

# TDOD
# setup

init
repos
ut
if $opts[for-win]; then
    win_mozc
else
    $env[os]_mozc
fi
